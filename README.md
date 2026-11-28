# Ventilo Zwift — boîtier de commande Bluetooth

Boîtier qui pilote un ventilateur 230 V depuis un téléphone Android, en manuel ou automatiquement selon la fréquence cardiaque ou la puissance du home trainer.

- **Appli** : `index.html`, servie par GitHub Pages → `https://carcobenjamin-max.github.io/ventilo-zwift/`
- **Programme de la carte** : `firmware/ventilo_ble/ventilo_ble.ino`
- **Mode d'emploi pour l'utilisateur** : [NOTICE.md](NOTICE.md)

Jusqu'à 4 profils par boîtier (nom, ceinture, plages FC/puissance) ; le profil se choisit tout seul selon la ceinture détectée. Le même programme et la même appli servent pour tous les boîtiers. Chaque boîtier s'annonce sous un nom unique `Ventilo-Zwift-XXXX` (XXXX tiré de la puce).

---

## 1. Ventilateur compatible

Le boîtier fait varier la tension par découpage de phase (triac). Ça ne convient qu'à un ventilateur **à moteur asynchrone avec sélecteur de vitesse mécanique** (molette ou boutons à crans), sans électronique.

| OK | À proscrire |
|---|---|
| Brasseur d'air ou ventilateur sur pied à sélecteur 3 vitesses mécanique (ex. HF-45A) | Ventilateur à télécommande, à touches tactiles, à minuterie ou à affichage |
| | Ventilateur « DC » / basse consommation / silencieux à moteur électronique |
| | Ventilateur à bloc d'alimentation externe |

Sur un ventilateur à carte électronique, le triac alimente la carte avec une tension découpée : au mieux ça ne marche pas, au pire la carte grille. Le sélecteur du ventilateur reste sur la **vitesse maxi**.

## 2. Matériel

| Composant | Référence / remarque |
|---|---|
| Arduino Nano ESP32 | ABX00083 — Gotronic code 38368 |
| Shield à borniers pour Nano | WPSH604 — Gotronic code 37781 |
| Alimentation USB-C 5 V ≥ 1 A | ex. Gotronic code 38572, ou tout chargeur USB-C fiable |
| Câble USB-C **données** | pour programmer la carte |
| Gradateur RobotDyn 8 A | AliExpress, variante **« 8A-Standard / Phase control »** (broches VCC, GND, Z-C, Dim). Pas la version UART/I2C. Vérifier la présence des optocoupleurs. |
| Embase secteur IEC avec porte-fusible | fusible 5×20 **temporisé T2A** |
| Prise femelle encastrable 2P+T | sortie vers le ventilateur |
| Câble souple 3G1 mm², cosses, serre-câbles | |
| Boîtier isolant | imprimé 3D ou du commerce, avec fixation des cartes et séparation 230 V / basse tension |

## 3. Câblage

**Basse tension (shield → gradateur)**

| Nano | Gradateur |
|---|---|
| 3V3 | VCC (**jamais 5 V** : le Z-C sort au niveau de VCC) |
| GND | GND |
| D2 | Z-C |
| D3 | Dim |

Option potentiomètre filaire (passer `USE_POT` à 1) : 3V3 / GND / curseur sur A0.

**230 V**

- Phase : embase → fusible T2A → borne **IN** du gradateur → borne **OUT** → phase de la prise femelle.
- Neutre : embase → borne **N** du gradateur (détection du zéro) et → neutre de la prise femelle.
- Terre : embase → terre de la prise femelle, en direct, sans coupure.
- Alimentation de la Nano : chargeur USB-C, branché en interne ou sur une prise à part.

## 4. Programmation de la carte

1. Arduino IDE 2 → gestionnaire de cartes → installer **« Arduino ESP32 Boards »** (par Arduino). Accepter les pilotes sous Windows.
2. Gestionnaire de bibliothèques → installer **« NimBLE-Arduino »** (par h2zero, version 2.x).
3. Ouvrir `firmware/ventilo_ble/ventilo_ble.ino`.
4. Carte **« Arduino Nano ESP32 »**, Tools → Pin Numbering → **By Arduino pin (default)**.
5. Téléverser **gradateur débranché du secteur**. En cas d'erreur « No DFU capable USB device » : double appui rapide sur reset, puis relancer.
6. Moniteur série à 115200 : le nom Bluetooth du boîtier s'affiche au démarrage.

Vérifié en compilation avec Arduino ESP32 Boards 2.0.17 + NimBLE-Arduino 2.5.2.

## 5. Mise en service (avant de remettre le boîtier)

**Hors tension**
- [ ] Continuité de terre embase ↔ prise femelle.
- [ ] Isolement phase/neutre ↔ terre (mégohmmètre 500 V, gradateur et Nano déconnectés si besoin).
- [ ] Fusible T2A en place, aucun conducteur 230 V dénudé accessible, câbles bridés.
- [ ] Séparation physique entre le 230 V et la basse tension.

**Sous tension, boîtier fermé**
- [ ] Appli connectée, mode manuel : le ventilo démarre à 100 % pendant 2 s puis descend.
- [ ] Voyant « Secteur OK » vert dans l'appli.
- [ ] Minimum moteur réglé (curseur sur 1 %, monter le minimum jusqu'à rotation franche sans ronflement).
- [ ] 30 à 45 min à mi-vitesse : carter moteur supportable à la main, gradateur à peine tiède. Sinon, monter le minimum.
- [ ] Capteurs de l'utilisateur associés et testés en mode Cardio et/ou Puissance.

**Règle** : ne jamais brancher l'USB d'un PC sur la Nano quand le boîtier est sur le secteur. Toute mise à jour se fait boîtier débranché.

## 6. Appli (GitHub Pages)

1. Dépôt public, `index.html` à la racine.
2. Settings → Pages → Source « Deploy from a branch » → Branch `main` / `(root)` → Save.
3. Après une minute : `https://carcobenjamin-max.github.io/ventilo-zwift/`.
4. Sur le téléphone : ouvrir l'adresse dans **Chrome Android**, menu ⋮ → « Ajouter à l'écran d'accueil ».

Safari (iPhone/iPad) ne gère pas le Bluetooth web : sur iPhone, utiliser le navigateur gratuit **Bluefy**.
