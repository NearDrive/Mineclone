# Anàlisi d'optimització FPS (focus: il·luminació i cost amb distància)

## Objectiu
Prioritzar FPS per sobre de la precisió física i adaptar el sistema actual perquè **només el detall proper (5-10 chunks)** tingui cost alt.

Regla de disseny proposada:

> "If it does not visibly improve gameplay, do not render or compute it."

---

## 1) Estat actual del motor (resum tècnic)

### Pipeline de render
- El render és **1 draw call per chunk visible** (`entry->mesh.Draw()`), després de culling per distància i frustum.
- Distància de render i culling ja existeixen, però segueixen mantenint malla per chunk (no hi ha agregació per zones).
- No hi ha boira obligatòria per ocultar popping/culling agressiu.

### Pipeline d'il·luminació
- El mesher calcula **llum per vèrtex** (`aSunlight`, `aEmissive`) i la passa al shader.
- Cada meshing fa:
  - `EnsureLightForNeighborhood(coord)` (3x3x3 chunks).
  - Mostreig de llum potencialment a chunk local + 6 veïns per cada vèrtex de cada cara visible.
- La llum es reconstrueix amb BFS per chunk (`RebuildLightForChunk`) sobre volum ampliat (`kChunkSize+2`) i dues passades (sun + emissive).
- Això implica cost CPU rellevant per chunk, sobretot quan hi ha edició de blocs o molts chunks entrant/sortint de rang.

### Filosofia de threading actual
- Generació i meshing CPU ja van a worker threads.
- Main thread manté pressupost d'uploads GPU per frame.
- Aquest punt està alineat amb la línia de "strict upload budget".

---

## 2) Avaluació del prompt proposat en aquest codi

### ✅ Estratègies molt compatibles

1. **Aggressive distance + frustum culling**
   - Ja existeix al projecte.
   - Es pot fer més agressiu en anells llunyans (retallar renderRadius efectiu segons càrrega frame).

2. **Chunk-level logic only, no per-block lighting (lluny)**
   - És la millor palanca: el cost actual és molt per-vèrtex i per-bloc.
   - Es pot mantenir detall proper i degradar lluny.

3. **CPU meshing in workers + main thread uploads with budget**
   - Ja implementat.
   - Es pot reforçar amb pressupostos separats per "near" i "far".

4. **Simple ambient + directional lighting**
   - Ja és l'enfoc actual del fragment shader.
   - Falta desacoblar la part de llum voxel detallada en distància.

5. **Mandatory fog to hide culling**
   - Altament recomanable amb el sistema actual per evitar popping visual quan fem optimitzacions agressives.

### ⚠️ Estratègies que encaixen però necessiten redisseny

1. **World divided into large zones**
   - Avui tot és per chunk.
   - Afegir "zones" (p.ex. 4x4 chunks) és viable, però afecta streaming, invalidació de malla i culling.

2. **One merged mesh per zone (very low draw calls)**
   - Gran guany en draw calls lluny, però no ho faria a tot arreu.
   - Millor model híbrid: chunks normals a prop, zones fusionades lluny.

### 🚫 No prioritari ara mateix

1. **No global illumination**
   - Ja no hi ha GI real; no és un problema actual.

2. **Very limited point lights, no shadows**
   - Ja esteu en aquest model (emissius simples + sense ombres).

---

## 3) Proposta concreta: LOD de render + il·luminació per anells (sense shaders avançats)

## Anells de distància (en chunks)
- **Near ring (0-5):** qualitat màxima actual.
- **Mid ring (6-10):** qualitat simplificada.
- **Far ring (>10):** qualitat extrema (mínim cost) o no render.

Això encaixa amb el requisit de "detall només a prop (5-10 chunks)".

### Near ring (0-5)
- Mantenir chunk mesh actual.
- Mantenir il·luminació voxel (sun + emissive) com ara.
- Mantenir remesh reactiu amb edició de blocs.

### Mid ring (6-10)
- Continuar en chunk-level, però:
  - Desactivar mostreig de llum per vèrtex detallat.
  - Guardar una sola "llum de chunk" (valor mitjà o màxim local) i aplicar-la uniforme.
  - Opcional: quantitzar normals/llum per reduir variància visual.
- Es redueix molt cost de `sampleLight` i de reconstrucció de llum detallada.

### Far ring (>10)
Opció A (simple i robusta):
- No renderitzar chunks normals.
- Boira obligatòria que tanqui abans del tall.

Opció B (més ambiciosa):
- Crear **zones fusionades** (p.ex. 4x4 chunks) amb malla simplificada.
- Sense llum per bloc: només ambient + directional fix + tint de boira.
- Una draw call per zona visible.

---

## 4) Canvis d'arquitectura recomanats (ordre de valor)

1. **LOD d'il·luminació per distància (primer pas, màxim ROI)**
- Afegir estat de "lighting tier" per chunk (Near/Mid/Far).
- Near: pipeline actual.
- Mid/Far: no reconstruir BFS complet cada cop; usar aproximació per chunk.
- Benefici: menys CPU en `RebuildLightForChunk` + menys cost en meshing.

2. **Boira obligatòria lligada a render distance**
- Definir `fogStart` i `fogEnd` en funció de `renderRadius`.
- Objectiu: amagar popping i canvis de LOD/culling.

3. **Pressupost dual d'uploads i meshing (near-first)**
- Separar cues o prioritzar feines near sobre far.
- Garantir estabilitat visual prop del jugador quan hi ha càrrega.

4. **Zones fusionades només per far ring (segona fase)**
- Mantenir edició detallada només near/mid.
- Rebuild de zona lazy i incremental.
- Benefici: draw calls dràsticament més baixes a distància.

---

## 5) Impacte esperat per component

- **CPU (il·luminació):** gran reducció si es talla BFS/mostreig per-vèrtex fora del near ring.
- **CPU (meshing):** reducció clara en mid/far per menys dades de llum i menys invalidacions.
- **GPU draw calls:** millora moderada sense zones; millora molt gran amb zones fusionades far.
- **VRAM/ample de banda:** millor si en mid/far simplifiqueu atributs de vèrtex i freqüència d'uploads.
- **Estabilitat frame-time:** millor amb pressupost near-first i boira.

---

## 6) Riscos i mitigacions

- **Popping de LOD/culling**
  - Mitigar amb boira i histèresi en canvi d'anell (no canviar LOD instantani cada frame).

- **Cost de mantenir dos camins de meshing**
  - Reutilitzar mesher amb flags de qualitat (no duplicar sistema sencer).

- **Desalineació visual entre anells**
  - Suavitzar amb transició de boira + blend curt de llum de chunk.

- **Complexitat de zones fusionades**
  - Fer-ho només per far ring i sense suport d'edició en temps real de màxima precisió.

---

## 7) Pla d'implementació suggerit (sense tocar shaders avançats encara)

### Fase 1 (ràpida, alt impacte)
- Introduir anells Near/Mid/Far.
- Near = actual.
- Mid/Far = llum simplificada de chunk (sense per-bloc/per-vèrtex detallat).
- Activar boira obligatòria.

### Fase 2 (estabilització)
- Priorització de cues per proximitat.
- Histèresi de LOD + telemetria de "temps per ring".

### Fase 3 (guany GPU fort)
- Zones fusionades al far ring (4x4 chunks com a punt inicial).
- Culling per zona + draw call únic per zona.

---

## 8) Conclusió
El prompt és **funcional i molt adient** per aquest projecte, especialment si s'aplica com a model híbrid:
- **Detall alt a prop** (5 chunks).
- **Detall simplificat en 6-10 chunks**.
- **Render mínim + boira fora d'això**.

La millora més rendible immediata no és tant "més shader", sinó **retallar el cost de llum i meshing per distància** i **prioritzar la feina visible pel jugador**.
