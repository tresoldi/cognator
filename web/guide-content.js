/*
 * cognator demo — curated examples + guide steps.
 * index.html reads GUIDE_STEPS to populate the examples dropdown and
 * the step-by-step sidebar.
 *
 * Each step has {title, body, example?}:
 *   - title: short label used in dropdown + step heading
 *   - body:  HTML for the guide sidebar
 *   - example: a CSV string loaded into the editor on click
 */

const POLYNESIAN = `ID,Language_ID,Concept,Segments,Cognacy
haw_water,Hawaiian,WATER,w a i,1
ton_water,Tongan,WATER,v a i,1
sam_water,Samoan,WATER,v a i,1
mao_water,Maori,WATER,w a i,1
tah_water,Tahitian,WATER,v a i,1
haw_fire,Hawaiian,FIRE,a h i,2
ton_fire,Tongan,FIRE,a f i,2
sam_fire,Samoan,FIRE,a f i,2
mao_fire,Maori,FIRE,a h i,2
tah_fire,Tahitian,FIRE,a u a h i,2
haw_eye,Hawaiian,EYE,m a k a,3
ton_eye,Tongan,EYE,m a t a,3
sam_eye,Samoan,EYE,m a t a,3
mao_eye,Maori,EYE,m a t a,3
tah_eye,Tahitian,EYE,m a t a,3
haw_house,Hawaiian,HOUSE,h a l e,4
ton_house,Tongan,HOUSE,f a l e,4
sam_house,Samoan,HOUSE,f a l e,4
mao_house,Maori,HOUSE,w h a r e,4
tah_house,Tahitian,HOUSE,f a r e,4
haw_hand,Hawaiian,HAND,l i m a,5
ton_hand,Tongan,HAND,n i m a,5
sam_hand,Samoan,HAND,l i m a,5
mao_hand,Maori,HAND,r i ŋ a,5
tah_hand,Tahitian,HAND,r i m a,5
haw_stone,Hawaiian,STONE,p ō h a k u,6
ton_stone,Tongan,STONE,m a k a,7
sam_stone,Samoan,STONE,m a ʔ a,7
mao_stone,Maori,STONE,p ō h a t u,6
tah_stone,Tahitian,STONE,o f a ʔ i,8
`;

const ROMANCE = `ID,Language_ID,Concept,Segments,Cognacy
lat_night,Latin,NIGHT,n o k t e,1
fra_night,French,NIGHT,n ɥ i,1
spa_night,Spanish,NIGHT,n o ʧ e,1
ita_night,Italian,NIGHT,n ɔ t t e,1
por_night,Portuguese,NIGHT,n o j t i,1
lat_water,Latin,WATER,a k w a,2
fra_water,French,WATER,o,2
spa_water,Spanish,WATER,a ɣ w a,2
ita_water,Italian,WATER,a k k w a,2
por_water,Portuguese,WATER,a g w a,2
lat_black,Latin,BLACK,n i g e r,3
fra_black,French,BLACK,n w a ʁ,3
spa_black,Spanish,BLACK,n e ɣ r o,3
ita_black,Italian,BLACK,n e r o,3
por_black,Portuguese,BLACK,n e g r u,3
lat_stone,Latin,STONE,p e t r a,4
fra_stone,French,STONE,p j ɛ ʁ,4
spa_stone,Spanish,STONE,p j e ð r a,4
ita_stone,Italian,STONE,p j ɛ t r a,4
por_stone,Portuguese,STONE,p e d r a,4
lat_good,Latin,GOOD,b o n u s,5
fra_good,French,GOOD,b ɔ̃,5
spa_good,Spanish,GOOD,b w e n o,5
ita_good,Italian,GOOD,b w ɔ n o,5
por_good,Portuguese,GOOD,b õ,5
lat_white,Latin,WHITE,a l b u s,6
fra_white,French,WHITE,b l ɑ̃,7
spa_white,Spanish,WHITE,b l a n k o,7
ita_white,Italian,WHITE,b j a n k o,7
por_white,Portuguese,WHITE,b r ã k u,7
`;

// Grimm's-law-flavoured Germanic vs Latin/Greek. Gives clearly
// related Indo-European forms across the p>f, t>θ, k>h shift.
const GERMANIC = `ID,Language_ID,Concept,Segments,Cognacy
lat_father,Latin,FATHER,p a t e r,1
grc_father,Greek,FATHER,p a t ē r,1
eng_father,English,FATHER,f ɑ ð ə r,1
deu_father,German,FATHER,f a t ɐ,1
got_father,Gothic,FATHER,f a d a r,1
lat_foot,Latin,FOOT,p e s,2
grc_foot,Greek,FOOT,p o u s,2
eng_foot,English,FOOT,f ʊ t,2
deu_foot,German,FOOT,f u s,2
got_foot,Gothic,FOOT,f o t u s,2
lat_three,Latin,THREE,t r e s,3
grc_three,Greek,THREE,t r e j s,3
eng_three,English,THREE,θ r i,3
deu_three,German,THREE,d r a ɪ,3
got_three,Gothic,THREE,θ r e i s,3
lat_heart,Latin,HEART,k o r,4
grc_heart,Greek,HEART,k a r d i a,4
eng_heart,English,HEART,h ɑ r t,4
deu_heart,German,HEART,h ɛ ʁ t s,4
got_heart,Gothic,HEART,h a i r t o,4
lat_hundred,Latin,HUNDRED,k e n t u m,5
grc_hundred,Greek,HUNDRED,h e k a t o n,5
eng_hundred,English,HUNDRED,h ʌ n d r ə d,5
deu_hundred,German,HUNDRED,h ʊ n d ɐ t,5
got_hundred,Gothic,HUNDRED,h u n d,5
`;

// Synthetic toy case designed to exhibit the whole span of
// confidence signals: tight clusters, unsupported singletons,
// ambiguous boundary cases, and true-singleton gold classes.
const CONFIDENCE_SHOWCASE = `ID,Language_ID,Concept,Segments,Cognacy
A_tight1,LectA,CAT,k a t,1
B_tight1,LectB,CAT,k a t,1
C_tight1,LectC,CAT,k a t,1
A_shift1,LectA,DOG,p i s,2
B_shift1,LectB,DOG,b i s,2
C_shift1,LectC,DOG,v i s,2
A_borderl,LectA,TREE,ʈ r i,3
B_borderl,LectB,TREE,t r i,3
C_borderl,LectC,TREE,ð r i,4
A_unique,LectA,STAR,s t a r,5
A_singl1,LectA,MOON,m u n,6
B_singl1,LectB,MOON,l u n a,7
C_singl1,LectC,MOON,o r b o,8
A_dense,LectA,BIRD,a v e,9
B_dense,LectB,BIRD,a v i s,9
C_dense,LectC,BIRD,a v e,9
`;

// A full Swadesh-style comparison: 27 concepts across Latin and six of its
// daughters, 189 forms. Large enough that the cluster graph, the sortable
// prediction table and the confidence columns all have something to show,
// and unlike the other examples it does not score near 1.0 -- which is the
// point of including it.
//
// Latin is given in the oblique/accusative stem the daughters actually
// descend from (n o k t e, not the nominative nox). Transcriptions are broad
// and one variety per language: European Portuguese, Eastern Catalan.
// Cognacy labels are etymon names rather than numbers, which the format
// accepts and which makes the gold column readable.
const ROMANCE_FULL = `ID,Language_ID,Concept,Segments,Cognacy
lat_water,Latin,WATER,a k w a,aqua
ita_water,Italian,WATER,a k k w a,aqua
spa_water,Spanish,WATER,a ɣ w a,aqua
por_water,Portuguese,WATER,a g w ɐ,aqua
cat_water,Catalan,WATER,a j ɣ w ə,aqua
fra_water,French,WATER,o,aqua
ron_water,Romanian,WATER,a p ə,aqua
lat_fire,Latin,FIRE,i g n e,ignis
ita_fire,Italian,FIRE,f w ɔ k o,focus
spa_fire,Spanish,FIRE,f w e ɣ o,focus
por_fire,Portuguese,FIRE,f o ɣ u,focus
cat_fire,Catalan,FIRE,f ɔ k,focus
fra_fire,French,FIRE,f ø,focus
ron_fire,Romanian,FIRE,f o k,focus
lat_night,Latin,NIGHT,n o k t e,nox
ita_night,Italian,NIGHT,n ɔ t t e,nox
spa_night,Spanish,NIGHT,n o t ʃ e,nox
por_night,Portuguese,NIGHT,n o j t ɨ,nox
cat_night,Catalan,NIGHT,n i t,nox
fra_night,French,NIGHT,n ɥ i,nox
ron_night,Romanian,NIGHT,n o a p t e,nox
lat_stone,Latin,STONE,p e t r a,petra
ita_stone,Italian,STONE,p j ɛ t r a,petra
spa_stone,Spanish,STONE,p j e ð r a,petra
por_stone,Portuguese,STONE,p e ð r ɐ,petra
cat_stone,Catalan,STONE,p ɛ ð r ə,petra
fra_stone,French,STONE,p j ɛ ʁ,petra
ron_stone,Romanian,STONE,p j a t r ə,petra
lat_head,Latin,HEAD,k a p u t,caput
ita_head,Italian,HEAD,t ɛ s t a,testa
spa_head,Spanish,HEAD,k a β e θ a,caput
por_head,Portuguese,HEAD,k ɐ b e s ɐ,caput
cat_head,Catalan,HEAD,k a p,caput
fra_head,French,HEAD,t ɛ t,testa
ron_head,Romanian,HEAD,k a p,caput
lat_eye,Latin,EYE,o k u l u,oculus
ita_eye,Italian,EYE,ɔ k k j o,oculus
spa_eye,Spanish,EYE,o x o,oculus
por_eye,Portuguese,EYE,o ʎ u,oculus
cat_eye,Catalan,EYE,u ʎ,oculus
fra_eye,French,EYE,œ j,oculus
ron_eye,Romanian,EYE,o k j,oculus
lat_hand,Latin,HAND,m a n u,manus
ita_hand,Italian,HAND,m a n o,manus
spa_hand,Spanish,HAND,m a n o,manus
por_hand,Portuguese,HAND,m ɐ̃ w̃,manus
cat_hand,Catalan,HAND,m a,manus
fra_hand,French,HAND,m ɛ̃,manus
ron_hand,Romanian,HAND,m ɨ n ə,manus
lat_tooth,Latin,TOOTH,d e n t e,dens
ita_tooth,Italian,TOOTH,d e n t e,dens
spa_tooth,Spanish,TOOTH,d j e n t e,dens
por_tooth,Portuguese,TOOTH,d e n t ɨ,dens
cat_tooth,Catalan,TOOTH,d e n,dens
fra_tooth,French,TOOTH,d ɑ̃,dens
ron_tooth,Romanian,TOOTH,d i n t e,dens
lat_tongue,Latin,TONGUE,l i ŋ g w a,lingua
ita_tongue,Italian,TONGUE,l i n g w a,lingua
spa_tongue,Spanish,TONGUE,l e ŋ g w a,lingua
por_tongue,Portuguese,TONGUE,l ĩ g w ɐ,lingua
cat_tongue,Catalan,TONGUE,ʎ e ŋ g w ə,lingua
fra_tongue,French,TONGUE,l ɑ̃ g,lingua
ron_tongue,Romanian,TONGUE,l i m b ə,lingua
lat_heart,Latin,HEART,k o r d e,cor
ita_heart,Italian,HEART,k w ɔ r e,cor
spa_heart,Spanish,HEART,k o r a θ o n,cor
por_heart,Portuguese,HEART,k o r ɐ s ɐ̃ w̃,cor
cat_heart,Catalan,HEART,k ɔ r,cor
fra_heart,French,HEART,k œ ʁ,cor
ron_heart,Romanian,HEART,i n i m ə,anima
lat_sun,Latin,SUN,s o l e,sol
ita_sun,Italian,SUN,s o l e,sol
spa_sun,Spanish,SUN,s o l,sol
por_sun,Portuguese,SUN,s ɔ l,sol
cat_sun,Catalan,SUN,s ɔ l,sol
fra_sun,French,SUN,s o l ɛ j,sol
ron_sun,Romanian,SUN,s o a r e,sol
lat_moon,Latin,MOON,l u n a,luna
ita_moon,Italian,MOON,l u n a,luna
spa_moon,Spanish,MOON,l u n a,luna
por_moon,Portuguese,MOON,l u ɐ,luna
cat_moon,Catalan,MOON,ʎ u n ə,luna
fra_moon,French,MOON,l y n,luna
ron_moon,Romanian,MOON,l u n ə,luna
lat_star,Latin,STAR,s t e l l a,stella
ita_star,Italian,STAR,s t e l l a,stella
spa_star,Spanish,STAR,e s t r e ʎ a,stella
por_star,Portuguese,STAR,ɨ s t r e l ɐ,stella
cat_star,Catalan,STAR,ə s t r e ʎ ə,stella
fra_star,French,STAR,e t w a l,stella
ron_star,Romanian,STAR,s t e a,stella
lat_rain,Latin,RAIN,p l u w j a,pluvia
ita_rain,Italian,RAIN,p j ɔ d dʒ a,pluvia
spa_rain,Spanish,RAIN,ʎ u β j a,pluvia
por_rain,Portuguese,RAIN,ʃ u v ɐ,pluvia
cat_rain,Catalan,RAIN,p l u ʒ ə,pluvia
fra_rain,French,RAIN,p l ɥ i,pluvia
ron_rain,Romanian,RAIN,p l o a j e,pluvia
lat_white,Latin,WHITE,a l b u,albus
ita_white,Italian,WHITE,b j a ŋ k o,blancus
spa_white,Spanish,WHITE,b l a ŋ k o,blancus
por_white,Portuguese,WHITE,b r ɐ̃ k u,blancus
cat_white,Catalan,WHITE,b l a ŋ,blancus
fra_white,French,WHITE,b l ɑ̃,blancus
ron_white,Romanian,WHITE,a l b,albus
lat_black,Latin,BLACK,n i g r u,niger
ita_black,Italian,BLACK,n e r o,niger
spa_black,Spanish,BLACK,n e ɣ r o,niger
por_black,Portuguese,BLACK,p r e t u,pretus
cat_black,Catalan,BLACK,n e ɣ r ə,niger
fra_black,French,BLACK,n w a ʁ,niger
ron_black,Romanian,BLACK,n e g r u,niger
lat_good,Latin,GOOD,b o n u,bonus
ita_good,Italian,GOOD,b w ɔ n o,bonus
spa_good,Spanish,GOOD,b w e n o,bonus
por_good,Portuguese,GOOD,b õ,bonus
cat_good,Catalan,GOOD,b ɔ,bonus
fra_good,French,GOOD,b ɔ̃,bonus
ron_good,Romanian,GOOD,b u n,bonus
lat_big,Latin,BIG,m a g n u,magnus
ita_big,Italian,BIG,g r a n d e,grandis
spa_big,Spanish,BIG,g r a n d e,grandis
por_big,Portuguese,BIG,g r ɐ̃ d ɨ,grandis
cat_big,Catalan,BIG,g r a n,grandis
fra_big,French,BIG,g ʁ ɑ̃,grandis
ron_big,Romanian,BIG,m a r e,mas
lat_new,Latin,NEW,n o w u,novus
ita_new,Italian,NEW,n w ɔ v o,novus
spa_new,Spanish,NEW,n w e β o,novus
por_new,Portuguese,NEW,n o v u,novus
cat_new,Catalan,NEW,n ɔ w,novus
fra_new,French,NEW,n œ f,novus
ron_new,Romanian,NEW,n o u,novus
lat_tree,Latin,TREE,a r b o r e,arbor
ita_tree,Italian,TREE,a l b e r o,arbor
spa_tree,Spanish,TREE,a r β o l,arbor
por_tree,Portuguese,TREE,a r v o r ɨ,arbor
cat_tree,Catalan,TREE,ə r β r ə,arbor
fra_tree,French,TREE,a ʁ b ʁ,arbor
ron_tree,Romanian,TREE,k o p a k,copac
lat_dog,Latin,DOG,k a n e,canis
ita_dog,Italian,DOG,k a n e,canis
spa_dog,Spanish,DOG,p e r o,perro
por_dog,Portuguese,DOG,k ɐ̃ w̃,canis
cat_dog,Catalan,DOG,g o s,gos
fra_dog,French,DOG,ʃ j ɛ̃,canis
ron_dog,Romanian,DOG,k ɨ j n e,canis
lat_fish,Latin,FISH,p i s k e,piscis
ita_fish,Italian,FISH,p e ʃ ʃ e,piscis
spa_fish,Spanish,FISH,p e θ,piscis
por_fish,Portuguese,FISH,p a j ʃ ɨ,piscis
cat_fish,Catalan,FISH,p e ʃ,piscis
fra_fish,French,FISH,p w a s ɔ̃,piscis
ron_fish,Romanian,FISH,p e ʃ t e,piscis
lat_bird,Latin,BIRD,a w e,avis
ita_bird,Italian,BIRD,u tʃ ɛ l l o,avicellus
spa_bird,Spanish,BIRD,p a x a r o,passer
por_bird,Portuguese,BIRD,p a s a r u,passer
cat_bird,Catalan,BIRD,u s e ʎ,avicellus
fra_bird,French,BIRD,w a z o,avicellus
ron_bird,Romanian,BIRD,p a s ə r e,passer
lat_name,Latin,NAME,n o m e n,nomen
ita_name,Italian,NAME,n o m e,nomen
spa_name,Spanish,NAME,n o m b r e,nomen
por_name,Portuguese,NAME,n o m ɨ,nomen
cat_name,Catalan,NAME,n ɔ m,nomen
fra_name,French,NAME,n ɔ̃,nomen
ron_name,Romanian,NAME,n u m e,nomen
lat_eat,Latin,EAT,k o m e d e r e,comedere
ita_eat,Italian,EAT,m a n dʒ a r e,manducare
spa_eat,Spanish,EAT,k o m e r,comedere
por_eat,Portuguese,EAT,k u m e r,comedere
cat_eat,Catalan,EAT,m ə n ʒ a,manducare
fra_eat,French,EAT,m ɑ̃ ʒ e,manducare
ron_eat,Romanian,EAT,m ɨ n k a,manducare
lat_drink,Latin,DRINK,b i b e r e,bibere
ita_drink,Italian,DRINK,b e r e,bibere
spa_drink,Spanish,DRINK,b e β e r,bibere
por_drink,Portuguese,DRINK,b ɨ b e r,bibere
cat_drink,Catalan,DRINK,b ɛ w r ə,bibere
fra_drink,French,DRINK,b w a ʁ,bibere
ron_drink,Romanian,DRINK,b e a,bibere
lat_sleep,Latin,SLEEP,d o r m i r e,dormire
ita_sleep,Italian,SLEEP,d o r m i r e,dormire
spa_sleep,Spanish,SLEEP,d o r m i r,dormire
por_sleep,Portuguese,SLEEP,d u r m i r,dormire
cat_sleep,Catalan,SLEEP,d u r m i,dormire
fra_sleep,French,SLEEP,d ɔ ʁ m i ʁ,dormire
ron_sleep,Romanian,SLEEP,d o r m i,dormire
`;

const GUIDE_STEPS = [
    {
        title: "Polynesian (classic example)",
        body: `<p>A five-lect Polynesian subset — Hawaiian, Tongan, Samoan, Māori, Tahitian — across seven concepts. This is the canonical worked example for Polynesian cognate detection. Well-behaved sound correspondences (e.g. /w/ ~ /v/ for WATER, /m a k a/ ~ /m a t a/ for EYE), a couple of mixed concepts (STONE has two gold classes), and one exotic form (<code>o f a ʔ i</code>) that should cluster as a singleton.</p>
            <p>Expected behaviour under the default preset:</p>
            <ul>
              <li>WATER, FIRE, EYE, HOUSE, HAND: all five lects cluster together per concept.</li>
              <li>STONE: split into two clusters matching the gold <code>6</code>/<code>7</code>/<code>8</code> labels.</li>
              <li>B-cubed F should be ≥ 0.95 — this is an easy case for the calibrated baseline.</li>
            </ul>`,
        example: POLYNESIAN,
    },
    {
        title: "Latin + Romance daughters",
        body: `<p>Six concepts across Latin + four Romance daughters (French, Spanish, Italian, Portuguese). Rough IPA transcription. Classic cognate cases (NIGHT, WATER, STONE, GOOD) plus one concept (WHITE) where Latin <code>a l b u s</code> belongs to a different cognate class than the shared Romance <code>b l a n k o</code>-type forms — a common historical split.</p>
            <p>Expected behaviour:</p>
            <ul>
              <li>NIGHT, WATER, BLACK, STONE, GOOD: all five forms per concept cluster together despite heavy phonetic drift (French <code>o</code> for WATER is still reliably clustered via context alignment).</li>
              <li>WHITE: splits into two clusters, Latin alone in class <code>6</code> and the four Romance daughters in class <code>7</code>.</li>
            </ul>`,
        example: ROMANCE,
    },
    {
        title: "Grimm's-law PIE reflexes",
        body: `<p>Five concepts with PIE cognates reflected across Latin, Ancient Greek (transliterated), and three Germanic branches (Old English, Old High German, Gothic). Grimm's law produces the characteristic p&nbsp;&gt;&nbsp;f, t&nbsp;&gt;&nbsp;θ, k&nbsp;&gt;&nbsp;h shifts that distinguish Germanic initials from the rest.</p>
            <p>Expected behaviour:</p>
            <ul>
              <li>All five concepts are single-cognate-class: the forms belong to one inherited root per concept.</li>
              <li>The detector should cluster all five lects together per concept despite the initial-consonant shift, because merkmal's feature-based distances recognise /p/ and /f/ (and /t/ and /θ/, /k/ and /h/) as closer than unrelated consonants.</li>
              <li>A good sanity check that the alignment layer is doing its job on non-trivial sound change.</li>
            </ul>`,
        example: GERMANIC,
    },
    {
        title: "Confidence-signal showcase",
        body: `<p>A synthetic case designed to exercise every region of the confidence-signal space:</p>
            <ul>
              <li><strong>CAT</strong> — identical forms across three lects. Cluster size 3, confidence 1.0, classic "low risk" example.</li>
              <li><strong>DOG</strong> — voicing chain p &gt; b &gt; v. Still one cluster, confidence somewhat lower thanks to more alignment distance.</li>
              <li><strong>TREE</strong> — borderline case. Gold splits the three forms into two classes (<code>3</code>/<code>4</code>) but the forms are phonetically close. Expect ambiguous output.</li>
              <li><strong>STAR</strong> — single-lect concept. A "true-singleton" gold class.</li>
              <li><strong>MOON</strong> — three completely unrelated forms; expect three singletons, each high-risk.</li>
              <li><strong>BIRD</strong> — all three from a single cognate class with minor variation.</li>
            </ul>
            <p>Useful for reading the per-prediction confidence columns and calibrated risk bands.</p>`,
        example: CONFIDENCE_SHOWCASE,
    },
    {
        title: "Romance, full comparison (189 forms)",
        body: `<p>Twenty-seven concepts across Latin and six daughters — Italian, Spanish, Portuguese, Catalan, French, Romanian — for 189 forms. The other examples here are small and score near 1.0; this one does not, and that is why it is worth running.</p>
            <p>What actually happens under the default preset:</p>
            <ul>
              <li>B-cubed precision <code>0.97</code>, recall <code>0.82</code>, F <code>0.89</code>. Eleven of the twenty-seven concepts are partitioned exactly as the gold column says.</li>
              <li>Precision far above recall means the clusters it forms are nearly all correct, and it splits things it should have joined. That asymmetry is the normal shape of an unsupervised run on divergent data, and it is what the confidence columns are for.</li>
            </ul>
            <p><strong>The interesting part is which forms it splits off.</strong> French is the systematic outlier — it separates in WATER, NIGHT, TONGUE, STAR, TREE, BLACK and DRINK. That is not a defect in the detector so much as a fact about French: it has the most eroded phonology in Romance, and <code>a k w a</code> → <code>o</code>, <code>s t e l l a</code> → <code>e t w a l</code>, <code>b i b e r e</code> → <code>b w a ʁ</code> leave very little surface material for an alignment to work with. A method that clustered those without supervision would be worth being suspicious of.</p>
            <p>Ten concepts contain a genuine lexical replacement rather than sound change. Three are recovered exactly:</p>
            <ul>
              <li><strong>FIRE</strong> — Latin <code>ignis</code> alone against the <code>focus</code> reflexes that replaced it everywhere else.</li>
              <li><strong>WHITE</strong> — inherited <code>albus</code> survives only in Latin and Romanian; the rest borrowed Germanic <code>blank</code>.</li>
              <li><strong>EAT</strong> — <code>comedere</code> in Iberia against <code>manducare</code> elsewhere.</li>
            </ul>
            <p>HEAD is worth a look for the opposite reason. The gold column puts Spanish <code>k a β e θ a</code> and Portuguese <code>k ɐ b e s ɐ</code> with Latin <code>caput</code>; the detector separates them. It is arguably right — those two continue <code>capitia</code>, not <code>caput</code> directly — which is a good reminder that a disagreement with the labels is not automatically an error in the model.</p>
            <p>Try the <em>Graph</em> tab on WATER or NIGHT to see the French form sitting outside the component, and sort the prediction table by confidence to find the boundary cases.</p>`,
        example: ROMANCE_FULL,
    },
];
