---
title: Calcul bottom-up d’attributs sur les arbres récursifs
author: Yann Orlarey
date: 2026-07-12
---

# Calcul bottom-up d’attributs sur les arbres récursifs

::: toc+
- **Objet** — définir le problème, le périmètre et les objectifs.
- **Modèle conceptuel** — séparer arbres, dépendances, algèbre, attributs et convergence.
- **Contrats génériques** — spécifier les paramètres de templates et leurs obligations.
- **Graphe de dépendances** — construire et condenser les dépendances avec DirectedGraph.
- **Algorithme d’évaluation** — calculer une fois les parties acycliques et itérer seulement dans les cycles.
- **Point fixe par encadrement** — représenter explicitement les approximations inférieure et supérieure.
- **Intégration avec FaustAlgebra** — appliquer une algèbre Faust aux nœuds de signaux.
- **Parcours algébrique des signaux** — définir le fold générique et sa loi de reconstruction modulo alpha-renommage.
- **API proposée** — donner une première forme d’interface C++ sans figer l’implémentation.
- **Diagnostics et terminaison** — rendre les échecs et les approximations observables.
- **Tests de conformité** — couvrir arbres partagés, récursions et politiques de convergence.
- **Migration** — introduire le mécanisme progressivement sans casser les usages existants.
- **Questions ouvertes** — recenser les décisions qui demandent encore une expérimentation.
:::

## Objet

Cette spécification décrit un moteur générique de calcul d’attributs *bottom-up* sur des arbres TLIB, y compris lorsque ceux-ci représentent des définitions récursives. Le moteur associe un attribut de type `Attribute` à chaque nœud utile et calcule cet attribut à partir de ceux dont le nœud dépend.

Le moteur reçoit exclusivement des arbres récursifs en représentation symbolique. Cette restriction est un invariant de l’API, et non un détail d’implémentation : elle permet de conserver le hash-consing des termes, y compris des sous-termes récursifs non clos. La représentation par indices de de Bruijn est hors périmètre, car un terme non clos ne peut pas y être hash-consé indépendamment de son contexte de liaison.

Sur une partie acyclique, chaque attribut ne doit être calculé qu’une fois. Sur une partie cyclique, les attributs doivent être réévalués jusqu’à ce qu’une politique configurable reconnaisse un point fixe ou une approximation finale sûre. La comparaison par égalité n’est pas imposée par le moteur.

Les objectifs sont les suivants :

- rendre le type d’attribut, son initialisation et sa politique de convergence paramétrables ;
- utiliser une `FaustAlgebra<Attribute>` pour l’interprétation locale des arbres de signaux ;
- préserver le partage structurel des arbres et mémoïser les résultats ;
- identifier les composantes fortement connexes avec DirectedGraph ;
- limiter les recalculs aux nœuds réellement affectés dans une composante cyclique ;
- fournir des diagnostics précis en cas de non-convergence.

Cette première version spécifie le comportement attendu. Elle ne fixe ni le nom définitif des classes ni leur emplacement final entre `signals`, TLIB et DirectedGraph.

::: note [Portée initiale]
La première implémentation peut vivre dans la bibliothèque `signals`, où le décodage des constructeurs de signaux et `FaustAlgebra` sont disponibles. La séparation des contrats doit néanmoins permettre de migrer ensuite le moteur générique dans TLIB.
:::

## Modèle conceptuel

Le système distingue cinq éléments.

Attribut
:   Valeur calculée pour un nœud. Exemples : intervalle, type, coût, ensemble de variables libres ou propriété de causalité.

Algèbre
:   Interprétation locale d’un constructeur. Elle produit le candidat d’un nœud à partir des attributs de ses dépendances.

Résolveur de dépendances
:   Traduit les nœuds symboliques récursifs de TLIB et leurs définitions en un graphe explicite de dépendances entre unités de calcul.

Politique de point fixe
:   Fournit les valeurs initiales et décide quand le candidat courant termine l’itération.

Ordonnanceur
:   Condense le graphe en composantes fortement connexes, évalue le DAG résultant dans l’ordre bottom-up et pilote une file de travail à l’intérieur des cycles.

```adt
AttributeState ::= Unknown
                 | Known(Attribute)

Component ::= Acyclic(Node)
            | Cyclic(Set(Node))

Step ::= Candidate(Attribute)
       | Stable(Attribute)
       | Changed(Attribute)
```

Pour un nœud $n$ de constructeur $op$ et de dépendances $d_1, …, d_k$, le candidat est :

```math
c_n = ⟦\mathrm{op}⟧_{\mathrm{Algebra}}(a_{d_1}, …, a_{d_k})
```

La composante est stable lorsque la politique accepte toutes les transitions pertinentes entre l’attribut précédent et ce candidat :

```math
\mathrm{Reached}(n, a_n, a'_n, contexte) = vrai
```

## Contrats génériques

### Type d’attribut

`Attribute` est un paramètre de template. Le moteur ne doit exiger implicitement ni constructeur par défaut, ni `operator==`, ni ordre total. Les opérations nécessaires sont fournies par les autres politiques.

Un attribut doit être copiable ou déplaçable selon le mode de stockage retenu. Une implémentation peut permettre des attributs immuables et partagés, par exemple `std::shared_ptr<const T>`, sans modifier l’algorithme.

### Algèbre locale

L’algèbre reçoit un nœud et les attributs déjà publiés de ses dépendances. Dans la spécialisation signaux, le décodage du nœud appelle l’opération correspondante de `FaustAlgebra<Attribute>`.

L’algèbre doit être déterministe relativement à ses arguments et à son contexte explicite. Elle ne décide ni de l’ordre global d’évaluation ni de la terminaison du point fixe.

### Initialisation

Une politique fournit la valeur initiale de chaque variable appartenant à une composante cyclique. Cette valeur peut dépendre du nœud et du contexte : élément bottom d’un treillis, intervalle maximal, type provisoire ou valeur définie par l’appelant.

Les nœuds acycliques n’ont pas besoin d’une valeur initiale : leur attribut est directement obtenu à partir de dépendances déjà calculées.

### Prédicat d’arrêt

Le prédicat reçoit au minimum la valeur précédente et la valeur courante. Il peut aussi consulter le nœud, le numéro d’itération, la phase et un état propre à la politique.

Il ne faut pas imposer `previous == current`. Un prédicat peut reconnaître une équivalence abstraite, une inclusion, une précision suffisante, une tolérance numérique ou l’épuisement d’un budget porté par l’attribut.

## Graphe de dépendances

### Orientation

Une arête `n → d` signifie que le calcul de `n` dépend de `d`. Cette convention correspond à celle de DirectedGraph et de ses `schedule`, où une dépendance doit précéder le nœud qui la consomme.

Les branches ordinaires d’un arbre engendrent donc des arêtes du parent vers les enfants. Le partage structurel de TLIB garantit qu’un même sous-arbre ordinaire correspond à un même nœud de graphe.

### Récursion

Les formes récursives symboliques TLIB ne doivent pas être décrites comme un couple de nœuds distincts `rec(id, body)` et `ref(id)`. Concrètement :

- `rec(id, body)` construit ou retrouve par hash-consing le nœud symbolique interné `SYMREC(id)` ;
- le corps `body` est attaché à ce nœud par la propriété récursive `RECDEF` ;
- une occurrence récursive est ce même `Tree` symbolique partagé, et non un nœud de référence distinct ;
- dans les signaux Faust, le corps peut représenter un groupe et ses projections.

Dans les signaux Faust, `RECDEF(R)` est une liste `(s₀, …, sₙ₋₁)` et le symbole `R` porte donc une valeur de groupe. L’analyse scalaire n’utilise pas le groupe entier comme une seule unité de calcul : elle introduit une unité logique distincte `(R, k)` pour chaque composante accessible par `proj(k, R)`.

Les dépendances structurelles sont :

```text
Expression(proj(k, R)) → RecursiveProjection(R, k)
RecursiveProjection(R, k) → Expression(RECDEF(R)[k])
```

Les expressions ordinaires restent identifiées par leur `Tree` hash-consé. Une projection récursive est identifiée par le couple formé du `Tree` symbolique partagé et de son indice. Aucun nouveau `Tree` TLIB n’est créé pour cette unité logique : elle appartient uniquement au graphe d’analyse.

Cette granularité évite de rendre cyclique un groupe entier lorsqu’une seule de ses composantes est récursive. Par exemple, si `R₀` est constant, `R₁` dépend seulement de `R₀` et `R₂` dépend de lui-même, seules les unités nécessaires au calcul de `R₂` appartiennent à une composante fortement connexe cyclique.

Un groupe symbolique récursif dépourvu de définition, une définition qui n’est pas une liste, un accès direct au groupe sans projection ou un indice hors limites est une erreur de construction et non une valeur bottom silencieuse. Le moteur n’accepte pas de forme de de Bruijn à son entrée ; une éventuelle conversion en représentation symbolique doit avoir lieu avant la préparation du plan.

```mermaid
flowchart LR
    T[Arbre TLIB symbolique hash-consé] --> R[Lecture de SYMREC et RECDEF]
    R --> G[Graphe de dépendances]
    G --> S[Composantes fortement connexes]
    S --> D[DAG de composantes]
    D --> E[Évaluation bottom-up]
```

### Composantes fortement connexes

Le moteur utilise `Tarjan<Node>` ou `graph2dag` de DirectedGraph. Une composante est cyclique si elle contient plusieurs nœuds ou si son unique nœud possède une boucle sur lui-même.

Le DAG des composantes est ordonnancé dépendances d’abord. Les composantes sans cycle sont évaluées une seule fois. Seules les composantes cycliques déclenchent une recherche de point fixe.

### Relations inverses

Pour éviter une réévaluation complète à chaque changement, le plan d’évaluation conserve aussi les dépendants immédiats. Si `n → d`, alors `n` appartient à `dependents[d]`. Lorsqu’un attribut de `d` change de manière significative, seuls ses dépendants dans la composante courante sont remis dans la file.

Le graphe, sa condensation, l’ordre des composantes et les relations inverses forment un plan réutilisable tant que la structure de l’arbre ne change pas.

## Algorithme d’évaluation

### Préparation

```algorithm "Construction du plan"
Input: racine root et résolveur de dépendances resolver
Output: plan d’évaluation plan
G ← resolver.build(root)
SCC ← Tarjan(G).partition()
DAG ← condensation de G selon SCC
order ← ordonnancement de DAG, dépendances d’abord
dependents ← arêtes inverses de G
return Plan(G, SCC, DAG, order, dependents)
```

### Composante acyclique

Pour une composante réduite à un nœud sans boucle, toutes les dépendances ont déjà un attribut publié. Le moteur appelle l’algèbre une fois, stocke le résultat et passe à la composante suivante.

### Composante cyclique

```algorithm "Point fixe local par file de travail"
Input: composante cyclique C, algèbre A, politique P et attributs externes cache
Output: attribut stable de chaque nœud de C
for n in C do
  current[n] ← P.initial(n)
  enqueue(worklist, n)
end
while worklist n’est pas vide do
  n ← dequeue(worklist)
  next ← A.evaluate(n, attributesOfDependencies(n, current, cache))
  if not P.reached(n, current[n], next, context[n]) then
    current[n] ← next
    for user in dependents[n] ∩ C do
      enqueueIfAbsent(worklist, user)
    end
  else
    current[n] ← next
  end
  P.checkBudget(context)
end
publish current dans cache
```

La file ne doit pas contenir plusieurs fois le même nœud. Son ordre initial doit être déterministe. Un ordre local obtenu en coupant les arêtes de retour peut améliorer la convergence, mais ne doit pas changer le résultat spécifié par la politique.

::: warning [Condition de propagation]
Le prédicat `reached` sert ici à décider si une transition peut être ignorée par les dépendants. Une politique dont la notion de convergence n’est pas locale doit fournir un contrôle au niveau de la composante, ou demander une passe finale complète avant publication.
:::

## Point fixe par encadrement

### Intervalle d’intervalles

Pour le calcul d’intervalles Faust, l’attribut récursif n’est pas un intervalle isolé mais un encadrement de l’intervalle recherché :

```cpp
struct IntervalEnclosure {
    interval lower;       // I₀ : approximation intérieure
    interval upper;       // I₁ : approximation extérieure sûre
    std::size_t iteration;
};
```

Si `X` désigne l’intervalle recherché, l’invariant est :

```math
I₀ ⊆ X ⊆ I₁
```

Le compteur `iteration` appartient à l’attribut. Le moteur générique n’a donc pas à connaître les notions d’élargissement, de rétrécissement ou d’âge d’une borne.

### Itération simultanée des bornes

Pour une fonction de transfert monotone `F`, l’algèbre d’encadrement applique l’algèbre d’intervalles indépendamment aux deux bornes :

```math
I₀' = F(I₀)
```

```math
I₁' = F(I₁)
```

et produit directement le nouvel attribut `(I₀', I₁', n + 1)`. Si l’initialisation vérifie `I₀⁰ ⊆ F(I₀⁰)` et `F(I₁⁰) ⊆ I₁⁰`, la monotonie garantit que les deux suites resserrent l’encadrement dans des directions opposées :

```text
I₀⁰ ⊆ I₀¹ ⊆ I₀² ⊆ … ⊆ X
X ⊆ … ⊆ I₁² ⊆ I₁¹ ⊆ I₁⁰
```

Il n’existe donc aucune transformation entre le candidat produit par l’algèbre et la valeur courante publiée par le moteur.

### Arrêt et résultat

Le prédicat reconnaît deux causes normales d’arrêt :

```cpp
bool reached(const IntervalEnclosure& x, std::size_t limit)
{
    return equivalent(x.lower, x.upper)
        || x.iteration >= limit;
}
```

Si `lower == upper`, le résultat est exact. Si le seuil est atteint avant leur rencontre, `upper` est retenu comme approximation finale sûre.

::: note [Point fixe ou approximation]
Au seuil, `upper` n’est pas nécessairement égal au point fixe mathématique. Le résultat normatif est une approximation extérieure sûre de celui-ci. Cette propriété suffit au calcul d’intervalles du compilateur, qui doit avant tout ne pas sous-estimer les valeurs possibles.
:::

### Conditions de correction

La correction de l’encadrement exige :

- une fonction de transfert `F` monotone pour l’inclusion ;
- une initialisation telle que `I₀⁰ ⊆ X ⊆ I₁⁰` ;
- les conditions `I₀⁰ ⊆ F(I₀⁰)` et `F(I₁⁰) ⊆ I₁⁰` ;
- des opérations d’intervalles qui préservent l’inclusion ;
- une égalité des bornes cohérente avec la propriété analysée ;
- le choix systématique de `upper` lors d’un arrêt par seuil.

Pour un groupe récursif, chaque projection porte son propre encadrement. Le compteur peut être local à la projection ou partagé par composante fortement connexe ; cette décision relève de la politique d’intervalles, pas du moteur générique.

### Généralité du moteur

Les autres attributs ne sont pas obligés d’utiliser un encadrement. Un domaine fini peut retourner directement son candidat et s’arrêter par égalité ; une analyse numérique peut employer une tolérance. Le moteur ne requiert dans tous les cas que :

1. `initial(node)` ;
2. le calcul algébrique du candidat ;
3. `reached(node, previous, candidate)`.

## Intégration avec FaustAlgebra

### Rôle

`FaustAlgebra<Attribute>` reste l’interface des primitives Faust : constantes, opérations numériques, délais, tables, éléments d’interface, etc. Un adaptateur de signaux :

1. reconnaît le constructeur du `Tree` ;
2. récupère les attributs de ses opérandes dans le cache ;
3. appelle la méthode correspondante de l’algèbre ;
4. retourne le candidat au moteur générique.

Le traitement structurel des nœuds symboliques récursifs `SYMREC`, de leur propriété `RECDEF`, des listes et des projections appartient au résolveur/adaptateur, et non aux opérations numériques de l’algèbre. Aucune opération de l’algèbre ne doit matérialiser un nœud de référence récursive distinct.

### `FixPointUpdate` existant

L’actuelle méthode virtuelle `FaustAlgebra<T>::FixPointUpdate(x, y)` n’a plus de rôle dans ce modèle. L’algèbre calcule directement un attribut complet ; dans le cas des intervalles, cet attribut est `IntervalEnclosure` et contient déjà les deux bornes ainsi que le compteur.

`FixPointUpdate` doit être dépréciée, puis retirée lors d’une évolution d’API explicitement annoncée. Pendant la transition, elle peut rester implémentée pour préserver la compatibilité binaire ou source, mais le nouveau parcours ne l’appelle pas.

## Parcours algébrique des signaux

### Intention

La bibliothèque doit fournir une forme générique de parcours des arbres de signaux qui interprète chaque constructeur au moyen d’une `FaustAlgebra<T>`. Ce parcours est un *fold* algébrique : les sous-signaux sont interprétés avant le signal qui les contient, puis le constructeur courant est traduit en un appel de l’algèbre.

Pour un signal non récursif, le principe est :

```math
Fold_A(op(s₁, …, sₙ)) = A.op(Fold_A(s₁), …, Fold_A(sₙ))
```

Par exemple :

```text
sigMul(sigInput(0), gain)
```

est interprété comme :

```cpp
algebra.Mul(
    algebra.Input(algebra.IntNum(0)),
    algebra.VSlider(label, init, lo, hi, step));
```

Le parcours ne contient pas la sémantique des opérations. Il reconnaît leur représentation `Tree`, ordonne leurs opérandes et délègue leur interprétation à l’algèbre.

### Décomposition en deux couches

Le système sépare deux responsabilités qui ne doivent pas être confondues.

Dispatcher algébrique
:   Reconnaît un constructeur Faust ordinaire et appelle exactement une méthode de `FaustAlgebra<T>` avec les attributs de ses opérandes dans l’ordre attendu.

Parcours structurel
:   Visite le DAG hash-consé, mémoïse les résultats, gère les groupes récursifs et projections, puis fournit les opérandes au dispatcher.

Cette séparation permet de réutiliser le même dispatcher dans deux modes :

- un fold syntaxique utilisé par l’algèbre de reconstruction ;
- l’évaluation sémantique par graphe de dépendances et point fixe utilisée par les analyses d’attributs.

```mermaid
flowchart LR
    T[Tree de signal] --> P[Parcours structurel]
    P --> D[Dispatcher exhaustif]
    D --> A[FaustAlgebra T]
    A --> V[Valeur T]
    P --> C[Mémo et contexte récursif]
```

### Contrat du dispatcher

Le dispatcher est une opération locale sans récursion interne :

```cpp
template <class T, class Algebra>
T applyFaustConstructor(Tree original,
                        const std::vector<T>& operands,
                        Algebra& algebra);
```

Son contrat est le suivant :

- chaque constructeur Faust pris en charge possède une branche explicite ;
- les opérandes sont sémantiques, et non nécessairement les branches TLIB brutes ;
- leur nombre et leur ordre correspondent exactement à la méthode de `FaustAlgebra` appelée ;
- les données immédiates comme un indice de canal ou une constante sont injectées par `IntNum`, `Int64Num`, `FloatNum` ou `Label` ;
- un constructeur inconnu produit un diagnostic `UnsupportedConstructor` ;
- aucune branche générique ne doit reconstruire silencieusement un nœud inconnu à partir de ses branches.

Par exemple, le nœud TLIB d’une opération binaire contient aussi son code d’opération, mais seuls ses deux signaux opérandes sont des dépendances calculées. De même, le label d’un slider est injecté par `Label`, tandis que `init`, `lo`, `hi` et `step` sont ses dépendances numériques.

### Groupes récursifs et projections

`FaustAlgebra<T>` ne contient actuellement ni opération `RecGroup` ni opération `Projection`. Ces formes ne sont pas des primitives numériques : elles contrôlent le partage et la récursion. Elles appartiennent donc, dans cette version de la spécification, au parcours structurel.

Les deux modes ne leur donnent pas la même interprétation :

- le calcul d’attributs résout `proj(k,R)` vers la composante logique `(R,k)` et recherche son point fixe ;
- la reconstruction conserve `proj(k,R)` comme référence syntaxique et ne développe pas `RECDEF(R)[k]` à cet emplacement.

Une projection ne doit donc pas être traitée par un simple fold eager qui interpréterait immédiatement sa définition : un tel parcours déplierait indéfiniment un cycle. Le contexte structurel doit exposer une référence au groupe avant de parcourir ses composantes.

### Nouage pour la reconstruction

La reconstruction d’un groupe symbolique suit un protocole en deux phases :

```algorithm "Reconstruction d’un groupe récursif"
Input: groupe symbolique original R portant RECDEF(R) = (s₀, …, sₙ₋₁)
Output: groupe reconstruit R′
id′ ← créer un identifiant récursif frais
R′ ← ouvrir un groupe symbolique portant id′
enregistrer R ↦ R′ dans le contexte
for k from 0 to n - 1 do
  s′ₖ ← fold syntaxique de sₖ
  toute projection proj(j,R) rencontrée devient proj(j,R′)
end
attacher (s′₀, …, s′ₙ₋₁) comme RECDEF(R′)
return R′
```

L’ouverture préalable du groupe réalise le *knot tying*. Elle garantit que les projections rencontrées pendant la reconstruction pointent vers le groupe reconstruit sans développer son corps.

Le renommage frais est le comportement général et sûr. Le contexte de reconstruction est un environnement de renommage des lieurs récursifs ; il doit être empilé lors de l’entrée dans un groupe imbriqué et dépilé à sa sortie. Toute projection liée à `R` est reconstruite avec `R′`, ce qui évite la capture et respecte les récursions imbriquées.

Réutiliser l’identifiant de `R` n’est pas neutre : puisque `RECDEF` est une propriété attachée au nœud symbolique hash-consé, fermer le groupe avec un corps transformé pourrait modifier la définition du groupe source. Un parcours générique ne doit donc jamais réutiliser un identifiant existant lorsqu’il est susceptible d’attacher une nouvelle définition.

Un mode conservatif peut retourner directement un groupe source dont aucune composante n’a changé, sans réattacher `RECDEF`. Cette optimisation autorise ponctuellement l’identité de pointeur, mais elle ne fait pas partie de la loi sémantique générale.

### Algèbre de reconstruction

Une `TreeFaustAlgebra : FaustAlgebra<Tree>` reconstruit chaque primitive avec le constructeur de la bibliothèque de signaux correspondant :

```cpp
class TreeFaustAlgebra : public FaustAlgebra<Tree> {
public:
    Tree IntNum(int x) override { return sigInt(x); }
    Tree Input(const Tree& channel) override;
    Tree Add(const Tree& x, const Tree& y) override { return sigAdd(x, y); }
    Tree Mul(const Tree& x, const Tree& y) override { return sigMul(x, y); }
    Tree VSlider(const Tree& label, const Tree& init,
                 const Tree& lo, const Tree& hi,
                 const Tree& step) override
    {
        return sigVSlider(label, init, lo, hi, step);
    }
    // Une méthode par primitive Faust.
};
```

Le traitement de `RECDEF` et `sigProj` reste effectué par le parcours structurel autour de cette algèbre.

### Loi fondamentale : alpha-équivalence

Pour tout signal symbolique fermé `s` couvert par le dispatcher, le fold par l’algèbre de reconstruction doit produire un signal alpha-équivalent :

```math
Fold_{TreeAlgebra}(s) ≡_α s
```

En TLIB, la propriété normative se vérifie avec `areEquiv` :

```cpp
Tree rebuilt = foldSignal(signal, treeAlgebra);
TLIB_ASSERT(areEquiv(rebuilt, signal));
```

L’identité de pointeur est une propriété renforcée, limitée aux situations où aucun renommage n’est nécessaire :

1. les arbres non récursifs doivent être identiques par pointeur grâce au hash-consing ;
2. les sous-arbres partagés doivent rester partagés et n’être interprétés qu’une fois ;
3. un groupe récursif peut rester identique par pointeur uniquement si le parcours retourne directement le groupe source sans modifier sa propriété `RECDEF`.

Pour une reconstruction complète avec renommage systématique des lieurs, l’identité par pointeur d’un arbre récursif n’est ni attendue ni souhaitable.

### Autres lois de conformité

Les autres lois attendues sont :

Déterminisme
:   Une même algèbre et un même signal produisent le même résultat, à contexte explicite égal.

Mémoïsation
:   Deux occurrences du même `Tree` ordinaire partagent un unique résultat de fold.

Conservation de l’arité
:   Le dispatcher fournit exactement le nombre d’opérandes attendu par l’opération algébrique.

Conservation des immédiats
:   Constantes, indices, labels et paramètres non récursifs sont injectés sans perte d’information.

Fermeture récursive
:   Toute projection reconstruite désigne le groupe reconstruit correspondant, jamais le groupe d’une autre liaison.

Fraîcheur
:   L’identifiant d’un groupe reconstruit n’est égal à aucun identifiant libre ou lié déjà présent dans le contexte cible.

Alpha-invariance
:   Deux arbres sources alpha-équivalents donnent des résultats alpha-équivalents avec une algèbre insensible au nom concret des lieurs.

### Relation avec le calcul d’attributs

Le fold syntaxique et le moteur de point fixe partagent le dispatcher, mais pas nécessairement le même graphe de parcours.

- Le fold de reconstruction préserve les projections comme références et noue les groupes en deux phases.
- L’analyse d’attributs remplace les projections par des dépendances vers `(R,k)` et utilise Tarjan ainsi que la politique de convergence.

Dans les deux cas, une fois les opérandes disponibles, le calcul local d’un constructeur ordinaire est strictement le même appel à `FaustAlgebra<T>`. Cette factorisation évite que l’analyse d’intervalles, l’inférence de types, la reconstruction d’arbres et les analyses futures maintiennent chacune leur propre grand dispatch de constructeurs Faust.

### Complétude de FaustAlgebra

La loi de reconstruction sert également d’audit de l’interface. Si un constructeur de signal ne peut pas être reconstruit par une méthode de `FaustAlgebra`, l’une des décisions suivantes doit être explicite :

- ajouter la primitive manquante à `FaustAlgebra` ;
- classer le constructeur comme forme structurelle gérée par le parcours ;
- le déclarer hors périmètre avec un diagnostic précis.

L’objectif à terme est qu’aucun constructeur rencontré dans le corpus Faust ne tombe dans une catégorie implicite ou un traitement par défaut.

## API proposée

### Prototype expérimental

Un premier prototype header-only est disponible dans `signals/bottom-up-attributes.hh`. Il implémente la construction du graphe, la partition de Tarjan, l’ordonnancement du DAG condensé et une file de travail par composante cyclique. Son API reste expérimentale et sert à valider les contrats décrits ici avant toute migration vers TLIB.

Les tests de `tests/tests.cpp` couvrent actuellement un DAG avec partage, le point fixe entier `R = min(R + 1, 3)`, le point fixe asymptotique `X = (X + 2) / 2` arrêté par tolérance plutôt que par égalité exacte, ainsi qu’un groupe de trois signaux dont une seule projection est réellement cyclique.

Une première spécialisation par intervalles interprète aussi des constructeurs Faust réels. Une entrée audio reçoit l’attribut `[-1,1]`, un slider reçoit l’intervalle défini par ses bornes via `interval_algebra::VSlider`, et les opérations arithmétiques sont déléguées à `interval_algebra`. Le cas `Input(0) × VSlider(0,1)` produit ainsi `[-1,1]`, directement et à travers les projections d’un groupe de signaux.

Le prototype teste également `IntervalEnclosure` sur l’équation récursive `X = Input(0) + 0.5 × X`, dont l’intervalle fixe est `[-2,2]`. L’approximation inférieure part de `[0,0]`, la supérieure de `[-4,4]`, et le compteur porté par la projection récursive arrête le calcul au seuil. La borne supérieure finale contient `[-2,2]` sans transformation postérieure du candidat algébrique.

Pour ces constructeurs, le résolveur expose les dépendances sémantiques plutôt que les branches TLIB brutes : les deux opérandes d’une opération binaire, et les quatre paramètres numériques d’un slider sans son label. Cela maintient l’ordre des attributs attendu par l’évaluateur Faust.

Le prototype représente ces deux sortes d’unités avec `SignalAttributeNode` :

```cpp
struct SignalAttributeNode {
    Tree tree;
    int projection;  // -1 : expression ; >= 0 : composante du groupe tree
};
```

`SignalProjectionDependencies` réalise le passage entre `sigProj`, la composante logique `(R, k)` et le signal d’indice `k` dans `RECDEF(R)`.

La forme suivante exprime les responsabilités sans constituer encore une API figée :

```cpp
template <class Attribute,
          class Evaluator,
          class DependencyResolver,
          class FixpointPolicy>
class BottomUpAttributes {
public:
    using node_type = typename DependencyResolver::node_type;

    BottomUpAttributes(Evaluator evaluator,
                       DependencyResolver resolver,
                       FixpointPolicy policy);

    EvaluationPlan<node_type> prepare(node_type root) const;

    Attribute evaluate(node_type root);
    Attribute evaluate(const EvaluationPlan<node_type>& plan);

    const Attribute& attribute(node_type node) const;
    const EvaluationReport& report() const;
};
```

Une politique minimale pourrait suivre ce protocole :

```cpp
struct FixpointPolicy {
    Attribute initial(Node node, FixpointContext& context);

    bool reached(Node node,
                 const Attribute& previous,
                 const Attribute& current,
                 FixpointContext& context);
};
```

Pour l’usage Faust envisagé :

```cpp
using Analysis = BottomUpAttributes<
    IntervalEnclosure,
    FaustSignalEvaluator<IntervalEnclosure, IntervalEnclosureAlgebra>,
    TlibRecursiveDependencyResolver,
    IntervalEnclosureTermination>;
```

Les objets de politique peuvent porter un état et être passés par valeur, référence ou pointeur selon les contraintes de durée de vie. Le concept C++ exact et l’éventuelle compatibilité C++11/C++17 restent à décider.

## Diagnostics et terminaison

Chaque évaluation produit au minimum :

- le nombre de nœuds et d’arêtes du graphe ;
- le nombre et la taille des composantes cycliques ;
- le nombre d’évaluations par nœud et par composante ;
- le nombre d’itérations et la cause d’arrêt de chaque encadrement ;
- la cause d’arrêt de chaque composante ;
- le budget consommé et les nœuds encore instables en cas d’échec.

Une limite d’itérations, de changements ou d’évaluations est obligatoire comme garde-fou. Son dépassement doit produire un statut explicite `NotConverged`, accompagné de la composante et des dernières transitions. Le moteur ne doit pas publier silencieusement un résultat incomplet comme un point fixe valide.

Les autres erreurs comprennent au moins : entrée en représentation de de Bruijn, nœud `SYMREC` sans propriété `RECDEF`, définition de groupe qui n’est pas une liste, accès scalaire à un groupe sans projection, projection récursive invalide, constructeur de signal non pris en charge, attribut demandé avant calcul et politique ayant produit une valeur invalide.

## Tests de conformité

### Structure et mémoïsation

- arbre ordinaire sans partage : résultat bottom-up attendu ;
- DAG avec sous-arbre partagé : une seule évaluation du sous-arbre ;
- plusieurs racines évaluées avec un même plan ou cache ;
- ordre stable entre exécutions.

### Récursion

- boucle sur un seul nœud ;
- cycle de deux nœuds ;
- composantes cycliques successives dans le DAG condensé ;
- cycle alimenté par une dépendance acyclique ;
- récursions imbriquées et groupes avec projections ;
- nœud `SYMREC` sans définition correctement rejeté ;
- entrée de de Bruijn correctement rejetée.

### Politiques

- égalité exacte sur un domaine fini ;
- convergence par équivalence sans `operator==` ;
- tolérance numérique ;
- resserrement simultané des bornes inférieure et supérieure ;
- arrêt exact lorsque les deux bornes se rencontrent ;
- arrêt par seuil avec sélection de la borne supérieure ;
- préservation de l’invariant `lower ⊆ résultat ⊆ upper` à chaque étape ;
- oscillation détectée par dépassement de budget.

### Intégration Faust

- reconstruire chaque constructeur non récursif couvert avec `TreeFaustAlgebra` et obtenir le même `Tree` ;
- préserver le partage d’un sous-signal présent plusieurs fois ;
- reconstruire un groupe récursif avec des identifiants frais, sans dépliage, et vérifier ses projections ;
- vérifier systématiquement `areEquiv` sur les arbres récursifs reconstruits ;
- vérifier l’absence de capture dans des groupes récursifs imbriqués ;
- vérifier que la reconstruction ne modifie aucune propriété `RECDEF` de l’arbre source ;
- échouer explicitement sur tout constructeur absent du dispatcher ;
- comparer les résultats aux analyses existantes sur des signaux non récursifs ;
- comparer les types et intervalles sur les corpus récursifs ;
- vérifier les réponses impulsionnelles lorsque l’analyse influence le code généré ;
- mesurer le nombre de calculs avant et après ordonnanceur par composantes.

## Migration

1. Introduire dans `signals` un prototype avec `Attribute`, adaptateur Faust, politique et résolveur séparés.
2. Tester d’abord une algèbre simple sur des arbres ordinaires et partagés.
3. Ajouter la construction du graphe, Tarjan et l’évaluation des composantes cycliques.
4. Extraire un dispatcher exhaustif des constructeurs ordinaires vers `FaustAlgebra`.
5. Implémenter `TreeFaustAlgebra` et valider la loi de reconstruction sur les formes non récursives.
6. Ajouter le nouage structurel des groupes symboliques et valider récursion, projections et partage.
7. Implémenter l’attribut `IntervalEnclosure` et vérifier son invariant sur les récursions Faust.
8. Comparer le prototype à l’inférence récursive actuelle de `sigtyperules.cpp`.
9. Stabiliser les concepts et les noms après mesures sur le corpus Faust.
10. Migrer le moteur indépendant de Faust dans TLIB ; conserver dans `signals` le décodage des constructeurs Faust.
11. Déprécier `FaustAlgebra::FixPointUpdate` après migration des analyses vers les attributs autonomes.

Cette progression garde chaque étape testable et évite de coupler immédiatement TLIB à FaustAlgebra ou à la représentation particulière des signaux.

## Questions ouvertes

- La politique de convergence doit-elle décider localement par nœud, globalement par composante, ou offrir les deux niveaux ?
- Faut-il inclure directement DirectedGraph dans TLIB, ou injecter une stratégie de décomposition afin de garder TLIB indépendante ?
- Le plan doit-il accepter plusieurs racines et permettre une invalidation incrémentale ?
- Quel niveau du moteur est responsable de la parallélisation des composantes indépendantes ?
- Quel standard C++ minimal doit être préservé pour l’intégration au compilateur Faust ?
- Les attributs doivent-ils être stockés dans une table locale par évaluation, dans des propriétés TLIB optionnelles, ou dans un cache persistant explicitement fourni ?
- Comment formaliser la phase finale de vérification pour les politiques qui arrêtent sur approximation plutôt que sur égalité ?
