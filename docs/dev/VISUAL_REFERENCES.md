# Visual reference index

Third-party images are stored **locally only** (`research/visual-references/`,
`research/guide/`; both git-ignored, never packaged). They were used to measure
geometry/colours and to understand behaviour; no artwork is reused in the app.
All our QML is independently drawn.

| ID | Source (URL) | Model | App version | Shows | Real software? | Res. | Used for |
|---|---|---|---|---|---|---|---|
| G-09 | lewitt-audio.com/support/online-guides/connect2 (`control-center-guide`) | CONNECT 2 | CC 4.0.18 (older theme) | annotated main window | real | 720×373 | window proportions, control placement, numbered feature list |
| G-11/12 | same (`screenshot-2023-09-14…`) | CONNECT 2 | CC 4.0.18 | Settings page | real | 720×438 | settings layout |
| G-13 | same (`custom-button`) | CONNECT 2 | 4.0.18 | custom-button radio list | real | 429×341 | function list order |
| G-22 | same (`screenshot…15.46.49`) | CONNECT 2 | 4.0.18 | "No LEWITT device connected!" | real | 720×525 | disconnected state |
| B-GUI | bonedo.de/artikel/lewitt-connect-2-test/ (`Lewitt_Connect2_GUI.jpg`) | CONNECT 2 | CC 5.x (macOS, retina) | main window incl. status bar hint | real | 1548×929 | **primary**: palette, geometry, hint bar, meter zones |
| B-Intro | same (`…Intro-Dialog.jpg`) | CONNECT 2 | CC 5.x | onboarding overlay (Loopback step) | real | 1770×1062 | onboarding design |
| B-help | same (`…help-2048x1230.jpg`) | CONNECT 2 | CC 5.x | Autosetup wizard chooser | real | 2048×1230 | autosetup chooser |
| B-mute | same (`…mute-2048x1229.jpg`) | CONNECT 2 | CC 5.x | (mute/ hint capture) | real | 2048×1229 | not yet analysed in detail |
| B-main | same (`…main.jpg`) | CONNECT 2 | CC 5.x | main window, large | real | 5289×3173 | not yet analysed (very large) |
| D-66 | delamar.de/test/lewitt-connect-2-test/ (`…test_66.jpg`) | CONNECT 2 | CC 5.2.1, fw 1.2.1 | main window, active MUTE red, Dim active | real | 1920×1080 | active-state colours, fader caps |
| D-77 | same (`…test_77.jpg`) | CONNECT 2 | CC 5.2.1 | Settings (custom button open, toggles) | real | 1920×1080 | settings styling (lime switches) |
| D-20 | same (`…test_20.jpg`) | CONNECT 2 | — | hardware photo | hardware | 1920×1080 | hardware layout only |
| P-* | lewitt-audio.com/de/connect-2 (hero/autosetup/denoise/compressor/mute/custom/leds/cc_v3/monitoring/loopback, 2560×1440 / 2222×1482 / 1500×1500) | CONNECT 2 | marketing | promotional renderings | **promo renderings** | large | not used as geometry evidence (stylised); feature descriptions only |
| A-* | amazona.de/test-lewitt-connect-6-audiointerface/ | **CONNECT 6** | — | — | — | — | **not retrieved**: served an anti-bot proof-of-work challenge (Anubis). Not circumvented. CONNECT 6 is out of scope anyway |

## Conflicts / caveats
* Colours differ between the 4.0.18 guide screenshots (very dark: header `#0b0a0a`,
  inputs `#2b2b2b`, outputs `#0c0c0c`) and 5.x captures (header `#1e1d1e`,
  inputs `#3e3e3e`, outputs `#1f1d1e`). We target 5.x (matches the 5.2.2
  installer analysed). Retina/gamma differences between captures are not
  separable from theme changes.
* The 5.x window shows the **LEWITT wordmark** in the header; we use plain text.
* Bonedo/Delamar captures are macOS; the Windows window chrome differs.
* Whether Settings is a separate window or an in-window view is not
  determinable from the images (Delamar shows it without the main header).
