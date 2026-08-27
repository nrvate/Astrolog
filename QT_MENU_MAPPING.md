# Windows menu structure reference (for Qt GUI parity)

Extracted from `astrolog.rc`'s `menu MENU` block (the main menu bar),
cross-referenced against `resource.h` command IDs and their `WM_COMMAND`
handlers in `wdriver.cpp`. The resource is the source of truth for both
builds' menu bars.

One entry here is **not** upstream's: Setting / Object Selections, which
this fork adds to the resource and therefore to the Windows build as well
as this one. It is marked below. Everything else is Walter Pullen's.

Legend: `[D]` opens a dialog (`Dlg*` function via `WiDoDialog`) · `[T]` direct
flag toggle · `[S]` selects one of a set (chart type/mode/house system/color
— radio-button style) · `[A]` one-shot action, no persistent toggle · `[P]`
submenu (POPUP).

Top-level menus, in order: **File, Edit, View, Info, Setting, Chart,
Graphics, Animate, Help**

```
File [P]
  Open Chart... [D] cmdOpenChart
  Open Chart #2... [D] cmdOpenChart2
  ---
  Save Chart Info... [D] cmdSaveChart
  Save Chart Positions... [D] cmdSavePositions
  ---
  Save Program Settings... [D] cmdSaveSettings
  Other Formats [P]
    Open Charts in Folder... [D] cmdOpenDir
    Save Chart List... [D] cmdSaveList
    ---
    Save Chart Exchange... [D] cmdSaveAAF
    Save Chart Quick*Chart... [D] cmdSaveQuick
    Save Chart iCalendar... [D] cmdSaveCalendar
  ---
  Export Chart Text Output... [D] cmdSaveText
  Export Chart Bitmap... [D] cmdSaveBitmap
  Export Vector Format [P]
    Export Chart Metafile... [D] cmdSavePicture
    Export Chart PostScript... [D] cmdSavePS
    Export Chart SVG... [D] cmdSaveSVG
    Export Chart Wireframe... [D] cmdSaveWire
  ---
  Export as Wallpaper [P]  (all [A]: set wallpaper mode + save bitmap)
    Tile/Center/Stretch/Fit/Fill Bitmap  cmdSaveWallTile/Center/Stretch/Fit/Fill
  Open Bitmap [P]
    Open Chart Background... [D] cmdOpenBackground
    Open World Map... [D] cmdOpenWorld
  File Settings... [D] cmdSettingFile
  ---
  Print... [D] cmdPrint
  Print Setup... [D] cmdPrintSetup   (system print dialog, not a Dlg*)
  ---
  Exit [A] cmdFileExit

Edit [P]
  Enter Command Line... [D] cmdCommand   (DlgCommand)
  ---
  Run Macro (Normal/Shift/Control/Alt/Ctrl+Shift/Alt+Shift/Ctrl+Alt/Ctrl+Alt+Shift Set) [P] x8
    -- each is 12 uniform "Macro N" [A] items (cmdMacro01..cmdMacro96), F1-F12 x8 sets.
       96 user-programmable macro slots total; low priority, not enumerated line-by-line.
  ---
  Copy Chart Text Output [A] cmdCopyText
  Copy Chart Bitmap [A] cmdCopyBitmap
  Copy Vector Format [P]
    Copy Chart Metafile/PostScript/SVG/Wireframe [A] cmdCopyPicture/PS/SVG/Wire
  ---
  Paste [A] cmdPaste

View [P]
  Show Graphics [T] cmdGraphics
  Window Settings [P]
    Buffer Redraws [T] cmdWinBuffer
    Redraw Screen [A] cmdWinRedraw
    Clear Screen [A] cmdWinClear
    Hourglass on Redraw [T] cmdWinHourglass
    ---
    Chart Resizes Window [T] cmdChartResizesWindow
    Window Resizes Chart [T] cmdWindowResizesChart
    Size Chart to Window [A] cmdSizeChartToWindow
    Size Window to Chart [A] cmdSizeWindowToChart
    Size Window Full Screen [A] cmdSizeWindowFull
    ---
    Scroll Page Up/Down [A], Scroll to Beginning/End [A]
  Colored Text [T] cmdColoredText
  Set Colors... [D] cmdColor
  ---
  Show Interpretations [T] cmdInterpret
  Print Nearest Second [T] cmdSecond
  Parallel Aspects [T] cmdParallel
  Applying Aspects [T] cmdApplying

Info [P]
  Set Chart Info... [D] cmdSetInfo
  Chart for Now [A] cmdNow
  Default Chart Info... [D] cmdDefaultInfo
  ---
  Set Chart #2 Info... [D] cmdSetInfo2
  Charts #3 Through #6... [D] cmdSetInfoAll
  Chart List [P]
    Chart List... [D] cmdList
    ---
    Previous/Next Chart [A] cmdListPrev/Next
    ---
    First/Last Chart [A] cmdListFirst/Last
    ---
    Swap Chart #1 and #2 [A] cmdSwap12
  ---
  No Relationship Chart [S] cmdRelNo
  Comparison Chart [S] cmdRelComparison
  Synastry Chart [S] cmdRelSynastry
  Composite Chart [S] cmdRelComposite
  Time Space Midpoint Chart [S] cmdRelMidpoint
  ---
  Date Difference Chart [S] cmdRelDate
  Biorhythm Chart [S] cmdRelBiorhythm
  Transit and Natal [S] cmdRelTransit
  Progressed and Natal [S] cmdRelProgressed

Setting [P]
  Sidereal Zodiac [T] cmdSidereal
  Heliocentric [T] cmdHeliocentric
  House System [P]  -- 22 house systems, all [S]: Placidus, Koch, Campanus,
    Regiomontanus, Topocentric, Alcabitius, Krusinski, A.P.C., Savard-A, ---,
    Porphyry, Pullen(S.Ratio), Pullen(S.Delta), ---, Meridian, Morinus,
    Horizon, Carter P.Equat., Sunshine, Sripati, ---, Equal, Equal(MC),
    Whole, Vedic, Null  (cmdHouse00,01,03,05,08,09,10,18,21,06,12,13,04,07,17,19,20,16,02,11,14,15,22)
  House Settings [P]
    Solar Chart [T] cmdHouseSetSolar
    3D Houses [T] cmdHouseSet3D
    ---
    Show Decans [T], Show Dwads [T], Flip Signs with Houses [T], Geodetic Houses [T]
    ---
    Indian Wheel Order [T], Show Navamsas [T]
  Aspect Settings... [D] cmdAspect
  Object Settings... [D] cmdObject
  More Object Settings... [D] cmdObject2
  Object Selections... [D] cmdObjectSel  (added by this fork, to both builds)
  ---
  Restrictions... [D] cmdRes
  Star Restrictions... [D] cmdStar
  Transit Restrictions... [D] cmdResTransit  (same DlgRestrict as cmdRes)
  Planetary Moons [P]
    Moons Chart [S] cmdChartMoons
    Exoplanets Chart [S] cmdChartExo
    ---
    Moon Restrictions... [D] cmdMoons
    Moon Object Settings... [D] cmdObjectM
    ---
    Include Moons [T] cmdResMoons
    Include Body Centers (COB) [T] cmdResCOB
    ---
    Object Customization... [D] cmdCustom
    Star Customization... [D] cmdCustomS
  ---
  Include Minors/Cusps/Uranians/Dwarfs/Fixed Stars [T] x5
  ---
  Calculation Settings... [D] cmdSettingCalc
  Display Settings... [D] cmdDisplay

Chart [P]  -- all [S] (chart-type switch, no dialog) except last 3:
  Standard Radix, House Wheel, Aspect Midpoint Grid, Aspect List, Midpoint List,
  Local Horizon, Solar System Orbit, Gauquelin Sectors, Calendar, Influence,
  Esoteric, Astrocartography, Ephemeris, Arabic Parts, Rising and Setting,
  Nearest Cities  (cmdChartList/Wheel/Grid/Aspect/Midpoint/Horizon/Orbit/
  Sector/Calendar/Influence/Esoteric/AstroGraph/Ephemeris/Arabic/Rising/Local)
  ---
  Transits... [D] cmdTransit
  Progressions... [D] cmdProgress
  ---
  Chart Settings... [D] cmdChartSettings

Graphics [P]
  Draw Chart Sphere/World Map/Globe/Polar Globe/Telescope [S] x5
  ---
  Reverse Background [T], Monochrome [T], Square Screen [T]
  Character Scale [P]
    Small/Medium/Large/Huge [S] x4, ---, Decrease/Increase [A] x2, ---,
    Decrease Text/Increase Text [A] x2
  Chart Effects [P]
    Show Border/Show Chart Info/Show Info Sidebar [T] x3, ---,
    Thicker Lines/Antialias Lines/Show Glyph Labels/Show Glyphs on Aspect Lines [T] x4
  Map Effects [P]
    Show Constellations/Show Full Star List/Show Exoplanets/
    Show Constellation Lines [T] x4, ---, Show House Details/Show Equator/
    Show Cities [T] x3, ---, Use Detailed World Map/Use Ecliptic Axis [T] x2
  Map Orientation [P]  -- all [A] (nudge rotation/tilt/zoom by a step)
    Rotate West/East, ---, Tilt North/South, ---, Set Tilt to Zero, ---,
    Zoom Out/In
  ---
  Indian Style Charts [P]
    Show Indian Wheels [T], ---, Draw South/North/East Indian [S] x3
  Modify Display [A] cmdGraphicsModify
  Modify Chart [A] cmdChartModify
  Scribble Color [P]  -- 16 colors, all [S]: Black, White, Red, Green, Blue,
    Yellow, Magenta, Cyan, Gray, Lt.Gray, Maroon, Dk.Green, Dk.Blue, Maize,
    Purple, Dk.Cyan (cmdPen00,15,09,10,12,11,13,14,08,07,01,02,04,03,05,06)
  Graphics Settings... [D] cmdSettingGraphics

Animate [P]
  Do Animation [T] cmdAnimateNo
  Jump Rate [P]
    Update to Now [A], ---, Seconds/Minutes/Hours/Days/Months/Years/Decades/
    Centuries/Millennia [S] x9, ---, 1/10th/1/100th/1/1000th Seconds [S] x3
  Jump Factor [P]
    One through Nine Units [S] x9
  Reverse Direction [T], Pause Animation [T], Timed Exposure [T]
  ---
  Step Forward/Backward [A] x2
  Store Chart Info [A], Recall Chart Info [A]

Help [P]
  Open Documentation... [D]* cmdDocHelpfile  (*opens external doc, not a Dlg)
  More Documentation [P]  -- all [A], open external doc files
    Open Documentation, ---, Open Changes, Open License, ---, Open Website,
    Open Website Mirror
  Open Data Files [P]  -- all [A], open external data files
    Open Default Settings, ---, Open Atlas, Open Time Zone Changes, ---,
    Open Star List, Open Orbital Elements, Open Exoplanet List
  ---
  List Signs/Objects/Aspects/Constellations/Planet Info/Rays/General Meanings/
  Switches/Obscure Switches/Keystrokes/Credits [A] x11 (prints a text listing)
  ---
  Setup [P]  -- Windows-only installer actions (cmdSetupUser/All/Desktop/
    Extension, cmdUnsetup) -- NOT APPLICABLE to Linux, skip entirely
  About Astrolog... [D] cmdHelpAbout
```

## Notes

- Command IDs (`cmd*`) are the symbolic names from `resource.h`; cross-check
  there for exact values if needed.
- `[D]` items were confirmed by finding the command's `WM_COMMAND` case in
  `wdriver.cpp` and seeing it call `WiDoDialog(Dlg*, ...)`.
- Sibling `.rc` menu resources starting around line 607 are a **separate
  set of right-click context menus**, not part of the main menu bar, and
  this doc does not map them. **All 42 are ported** (plan item 1) and the
  test suite resolves every entry; this note is kept for the structure it
  records. There are two parallel families. The graphics ones are chosen by
  `gi.nMode` (`menuV`/`menuV2` for wheels, then `menuG`, `menuM`,
  `menuZ`, `menuS`, `menuH`, `menuK`, `menuJ`, `menu7`, `menuL`,
  `menuE`, `menuZd`, `menuN`, `menu8`, `menuB`, `menuY`, `menuXX`,
  `menuXG`, `menuXZ`), and the text-mode ones by the `us.f*` chart-type
  flags (`menu_V`, `menu_W`, `menu_G`, `menu_A`, `menu_M`, `menu_Z`, …).
  Windows dispatches both from `WM_RBUTTONDOWN` in wdriver.cpp (~line
  938) through `DoPopup()`. Their entries are ordinary `cmd*` commands
  that the main menu bar already implements, so porting them was mostly
  wiring rather than new behaviour — each Qt entry proxies to the menu bar
  action rather than reimplementing the command.
- Windows-only items (Setup submenu, Print Setup's native dialog) don't
  apply to the Qt/Linux build and should be skipped rather than stubbed.
