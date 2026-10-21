# AvatarCam – VRChat avatár virtuális kamera OBS-hez

[English](README.md) | **Magyar**

Windows-os program (Dear ImGui + Direct3D 11), amellyel a VRChat avatárodat (vagy bármilyen VRM / FBX / GLB modellt)
kameraképként használhatod OBS-ben, Discordban vagy bárhol, ahol az OBS Virtuális kamerája működik. Webkamerás arc- és
kézkövetés, mikrofonos szájmozgás és VMC fogadás is van benne.

A program felülete alapból angol. Magyarra váltás: **Output → Program → Language / Nyelv**.

## Letöltés és indítás

Töltsd le a legújabb zipet a [Releases](../../releases) oldalról, csomagold ki bárhová, és indítsd az `AvatarCam.exe`-t.
A mellette lévő `onnxruntime*.dll` fájlok és a `models` mappa az arc- és kézkövetéshez kellenek.
A beállítások, az importált avatárok és az előbeállítások az exe mellé kerülnek.

Minden helyben fut: a webkamera és a mikrofon képét/hangját csak a gépeden dolgozza fel, semmit nem küld el sehová.

## Avatár importálása

**Avatár → Importálás…**, vagy egyszerűen húzd a fájlt az ablakba.

| Formátum | Mi jön át |
|---|---|
| VRChat `.unitypackage` | modell, anyagok (Poiyomi / lilToon / Standard / MToon közelítés), textúrák, **a teljes VRChat menü** (ruha- és kiegészítő kapcsolók, szín csúszkák), FX animátor, pislogás, szájmozgás (viseme), szemcsontok, **PhysBone / DynamicBone fizika** |
| `.vrm` (0.x és 1.0) | modell, MToon, kifejezések (happy, angry…), rugós csontok, csomópont kényszerek |
| `.glb` / `.gltf` / `.fbx` | modell és anyagok, csontok név szerinti felismerése |
| `.zip` | a benne lévő legjobb modellt importálja |

A felnőtt tartalmú kiegészítő-rendszereket (DPS/TPS/SPS) az importáló kihagyja.

## A fülek

- **Avatár** – avatárlista, kézjelek (bal/jobb), VRM arckifejezések, a **VRChat menüd** (Clothing, Accs, Colour Options…), minden paraméter.
- **Testrészek** – minden testrész/ruhadarab ki-be kapcsolása (jobb klikk: vissza az avatár szerintire), anyagonként
  színezés, árnyalat, telítettség, fényerő, saját világítás szín, világítás erősség, átlátszóság; teljes színeltolás; alakkulcsok (blendshape).
- **Jelenet** – háttér (átlátszó, egyszínű, színátmenet, kép, chroma key), **virtuális fények** (irányított / pont / spot, szín, erősség, irány,
  árnyék, kövesse a kamerát; gyors beállítások: Stúdió, Neon, Naplemente, Drámai, Egyenletes), kamera, megjelenés:
  toon / PBR / lapos árnyalás, kontúrfény, körvonal, ragyogás (bloom), tónusleképezés, árnyékok, talajárnyék.
- **Mozgás** – testtartás (karok, könyök, csukló, ujjak), légzés, ringatózás, fejmozgás, automatikus pislogás (beállítható, hány másodperc
  között – pl. 3 és 6 mp –, opcionális dupla pislogással), szemmozgás, fizika.
- **Követés** – webkamerás arckövetés (fej, pislogás, száj, mosoly, szemöldök, szemek), **kézkövetés** (karok, csukló, ujjak),
  szájmozgás mikrofonról, VMC fogadás.
- **Kimenet** – felbontás, FPS, **háttér** (ugyanaz, mint a Jelenet fülön), OBS kapcsolat, Spout2, **teljesítmény**, előbeállítások,
  **globális gyorsbillentyűk** (játék közben is működnek), felület mérete, nyelv.

Az előnézetben: bal egérgomb = forgatás, jobb/középső = mozgatás, görgő = nagyítás, dupla kattintás = alap nézet.

## OBS beállítása

**A) Játékrögzítés (ajánlott, bővítmény nélkül)**
1. Kimenet fül → *Kimeneti ablak* bekapcsolása (vagy az előnézet feletti pipa).
2. OBS: Források → + → *Játékrögzítés (Game Capture)*.
3. Mód: *Adott ablak rögzítése*, Ablak: `[AvatarCam.exe]: AvatarCam Output`.
4. Pipáld be: **Átlátszóság engedélyezése**, és vedd ki a pipát a **Kurzor rögzítése** elől.

A kimeneti ablak akkor is folyamatosan ad képet, ha alt+tabbal elváltasz, vagy a játék van előtte. Kis méretre nem tehető
(ha mégis, pl. Win+D, magától visszaáll), és nincs rajta egérmutató. Egérrel húzható, jobb klikkre bezárul.

Amíg a kimeneti ablak be van kapcsolva, a fő ablakban nem látszik az avatár (a kép csak a kimeneti ablakban van);
a kamerát ott is forgathatod, mozgathatod és nagyíthatod az egérrel.

Ne a fő AvatarCam ablakot rögzítsd: azon a felület is látszik, kis méretre téve nem ad képet, és az egér is rákerülhet.

**B) Spout2** – a leggyorsabb, teljesen átlátszó kapcsolat; az OBS-hez a *Spout2 Plugin for OBS* bővítmény kell.
Kimenet fül → *Spout2 küldés*, OBS-ben forrás: *Spout2 Capture* → `AvatarCam`.

**C) Chroma key** – Jelenet → Háttér: *Chroma key*, OBS-ben ablakrögzítés + *Chroma Key* szűrő.

Ha az OBS-ben elindítod a **Virtuális kamerát**, az avatár Discordban, Zoomban stb. is kamerakép lesz.

## Játék melletti használat (teljesítmény)

A Kimenet → *Teljesítmény* rész mutatja, mennyi processzort és videókártyát használ a program.

- Ha az OBS 30 fps-sel streamel, állítsd az *FPS korlát*ot 30-ra (vagy nyomd meg a **Kímélő mód** gombot): ez felezi a renderelés terhelését.
- Ha az AvatarCam ablaka nincs előtérben, a felület csak ritkán frissül (*Felület FPS háttérben*); az OBS-be menő kép ettől teljes sebességű marad.
- Ha az avatár csak egy sarokban látszik a streamen, az 1280×720 felbontás is elég.
- A követés (arc, kéz) normál alatti prioritással fut, így a játék élvez elsőbbséget.
- Érdemes a játék FPS-ét korlátozni, hogy a videókártyán maradjon tartalék az OBS-nek és az avatárnak.

## Követés tippek

- Jó, szemből érkező megvilágítás és a kamera nagyjából szemmagasságban.
- Nézz egyenesen a kamerába, és nyomd meg a **Középre állítás** gombot.
- A pislogás és a száj az első 10–20 másodpercben önmagát kalibrálja.
- Telefonos követéshez (pl. Waidayo) használd a VMC fogadást (alapport: 39539).
- Kézkövetéshez a kezed legyen a kamera látóterében, jól megvilágítva. Ha a kéz eltűnik a képről,
  a kar lágyan visszaereszkedik az alaptartásba. A *Kamera kép mutatása* opcióval látod, mit érzékel a program.
- A kezek tükrözése külön állítható (Követés → Kézkövetés → *Tükrözés*): bekapcsolva a jobb kezed az avatár bal kezét
  mozgatja (mint egy tükörben), kikapcsolva a jobbat. Ha a kezek fordítva mozognak, ezt kapcsold át.

## Fordítás forrásból

Kell: Visual Studio 2022/2026 C++ eszközök, CMake, Ninja.

```bat
git clone --recursive https://github.com/FrankHUN88453/AvatarCam.git
cd AvatarCam
build.bat
```

Az eredmény: `bin\AvatarCam.exe`. A követéshez másold a kiadás zipjéből az `onnxruntime.dll`, `onnxruntime_providers_shared.dll`
fájlokat és a `models` mappát a `bin\` mappába.

Parancssori segédek: `AvatarCam.exe --import <fájl>` (importálás ablak nélkül),
`AvatarCam.exe --screenshot <kép.png> <mp>` (kimeneti kép mentése teszteléshez).

## Ismert korlátok

- A Poiyomi / lilToon shadereket közelítjük (szín, textúra, normal, emisszió, maszkok, matcap rétegek, színeltolás);
  a Poiyomi beépített matcap textúráit (nincsenek a csomagban) egy általános fém fény helyettesíti –
  Jelenet → Megjelenés és effektek → *Hiányzó Poiyomi matcap pótlása* (Sötét fém / Króm / Kihagyás).
- Részecskerendszerek (pl. lehelet füst) és Unity kényszerek (pl. kard rendszer) nem jelennek meg / nincsenek szimulálva.

## Külső komponensek

Dear ImGui (MIT), ufbx (MIT / public domain), cgltf (MIT), stb (MIT / public domain), nlohmann/json (MIT),
ONNX Runtime (MIT), OpenSeeFace modellek (BSD 2-Clause), MediaPipe kéz modellek – OpenCV Model Zoo (Apache 2.0),
Spout2 SDK (BSD 2-Clause). A licencek: `bin\licenses` (a kiadás zipjében: `licenses`).

## Licenc

Az AvatarCam az [MIT licenc](LICENSE) alatt érhető el. A külső komponensekre a saját licencük vonatkozik (lásd fent).
