# Ventilo Zwift — mode d'emploi

Ce boîtier règle la vitesse de ton ventilateur depuis ton téléphone. Il peut aussi l'adapter tout seul à ta fréquence cardiaque ou à ta puissance sur le home trainer.

**Appli** : `https://carcobenjamin-max.github.io/ventilo-zwift/` (à ouvrir dans Chrome sur Android)

---

## Sécurité

- Appareil **fait main**, alimenté en 230 V. **Ne jamais l'ouvrir.** Le débrancher avant toute intervention.
- Fusible **temporisé T2A (5×20)** dans l'embase. Ne pas mettre plus gros.
- Uniquement avec **le ventilateur fourni ou validé** : un ventilateur à moteur classique et sélecteur mécanique. Jamais un ventilateur à télécommande, à touches électroniques ou « DC ».
- Le sélecteur du ventilateur reste sur la **vitesse maxi** : c'est le boîtier qui règle.
- Ne pas couvrir le boîtier, le garder au sec (pas sous la sueur qui coule du vélo).
- Après la séance : ventilo à l'arrêt dans l'appli, ou boîtier débranché si tu ne t'en sers pas pendant longtemps.

## Première utilisation

1. Branche le ventilateur sur la prise du boîtier, puis le boîtier sur le secteur.
2. Sur le téléphone : Bluetooth **activé**, ouvre l'adresse de l'appli dans **Chrome**.
3. Appuie sur **Connecter**, choisis **Ventilo-Zwift-XXXX** dans la liste. Accepte « Appareils à proximité » si Chrome le demande.
4. Menu ⋮ de Chrome → **Ajouter à l'écran d'accueil** : l'appli s'ouvre ensuite comme une vraie appli.

## Les 3 modes

| Mode | Fonctionnement |
|---|---|
| **Manuel** | Le curseur ou les boutons Arrêt / 30 / 60 / 100. |
| **Cardio** | Le ventilo suit ta fréquence cardiaque (ceinture Bluetooth). |
| **Puissance** | Le ventilo suit tes watts (home trainer Bluetooth). |

- Au démarrage, le ventilo part à 100 % pendant 2 s pour se lancer, puis descend : c'est normal.
- En auto, il monte vite et redescend lentement, pour que tu restes ventilé après un effort.
- Toucher le curseur repasse en manuel.
- Une fois réglé, **le téléphone n'est plus nécessaire** : le boîtier se connecte tout seul à ta ceinture ou à ton home trainer.

## Associer ta ceinture ou ton home trainer

1. Section **Capteurs** → **Rechercher** sur la bonne ligne.
2. Réveille le capteur : ceinture portée et humidifiée, ou 2 tours de pédale.
3. Touche ton capteur dans la liste. Le point devient vert quand il envoie des mesures.

Si rien n'apparaît : ferme Zwift (ou l'appli qui utilise le capteur), associe-le ici, puis relance Zwift. Certains capteurs n'acceptent qu'une connexion Bluetooth à la fois ; dans ce cas, le mode correspondant ne marchera pas en même temps que Zwift.

## Réglages

Dans **Réglages → Plages de régulation** :

- **FC bas de plage / à 100 %** : par exemple ta FC d'endurance tranquille et ta FC de seuil.
- **Puissance** : entre ta FTP et appuie sur « 55 → 105 % FTP ».
- **Vitesse en bas de plage** : 0 = ventilo arrêté quand tu es en dessous.
- Appuie sur **Enregistrer dans le ventilo**. Les réglages restent dans le boîtier, même débranché.

## Voyants dans l'appli

| Voyant | Sens |
|---|---|
| Secteur vert | Tout va bien. |
| Secteur rouge | Le boîtier ne voit pas le secteur : vérifie le branchement, puis le fusible. |
| Capteur orange | Connecté mais pas de mesure (ceinture pas portée, pas de pédalage). |
| Capteur rouge | Capteur choisi mais injoignable (éteint ou pris par une autre appli). |

## En cas de souci

- **Le ventilo ronfle à basse vitesse** : Réglages → Minimum moteur, monte de 5 %.
- **L'appli ne trouve pas le boîtier** : Bluetooth activé ? Boîtier branché ? Autre téléphone déjà connecté dessus ? (un seul à la fois)
- **Plus rien ne marche** : débranche le boîtier 10 s, puis rebranche-le.
