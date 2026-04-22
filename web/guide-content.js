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
];
