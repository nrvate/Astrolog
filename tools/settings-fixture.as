@AD800  ; Astrolog settings fixture -- every line sets a value switch to a
; sentinel and declares (in its "EXPECT" comment) a regex that must match
; the file saved after loading this one. tools/settings-round-trip.sh leg 3
; extracts and checks them. A switch whose save-twin regresses stops
; matching. Flag switches are covered separately by leg 2's prefix flip.

-n  ; chart for the current moment, so no interactive prompt blocks -od
    ; EXPECT ^-n

-A 11              ; EXPECT ^-A 11
-c Whol            ; EXPECT ^-c Whol
:w 4               ; EXPECT ^:w 4
:I 87              ; EXPECT ^:I 87
-YQ 47             ; EXPECT ^-YQ 47
-Yz 11             ; EXPECT ^-Yz 11
-YP 1              ; EXPECT ^-YP 1
:Xs 300            ; EXPECT ^:Xs 300
:YXg 33            ; EXPECT ^:YXg 33
:YXj 5             ; EXPECT ^:YXj 5
:YXS 2.5           ; EXPECT ^:YXS 2\.5
:YX7 700           ; EXPECT ^:YX7 700
:YXa -5            ; EXPECT ^:YXa -5
-zv 555ft          ; EXPECT ^-zv 555ft
-zj "Fixture Name" "Fixture Place"  ; EXPECT Fixture Name
-YAo 5 5 6.5       ; EXPECT ^-YAo 1 5 .*6\.5
-YAm 5 5 123       ; EXPECT ^-YAm 0 10 .*123
-YAd 5 5 3         ; EXPECT ^-YAd 0 10 +1 1 1 0 0 3
-Yj 5 5 44         ; EXPECT ^-Yj 0 10 +30 30 25 10 10 44
-YjT 5 5 55        ; EXPECT ^-YjT 0 10 +10 10 +4 +8 +9 55
-YkO 5 5 Pink      ; EXPECT ^-YkO 0 10 .*Pin
-YkA 3 3 Sky       ; EXPECT ^-YkA 1 5 +Yel Blu Sky
-YJ Mar Cap 0      ; EXPECT ^-YJ Mar Cap
-YJ0 Mar Aqu       ; EXPECT ^-YJ0 Mar Aqu
-Y7O 5 5 7         ; EXPECT ^-Y7O 0 10 +3 +2 4 4 5 7
-Y7C 3 3 45        ; EXPECT ^-Y7C 1 12 +17 +4 45
-YR 5 5 1          ; EXPECT ^-YR 0 10 +1 0 0 0 0 1
-YRT 5 5 1         ; EXPECT ^-YRT 0 10 +1 0 1 0 0 1
-YjA 4 4 0.7       ; EXPECT ^-YjA 1 5 .*0\.7
-Fm 20 2 3         ; EXPECT ^-Fm 20 2 3
-YD 5 "Ares"       ; EXPECT ^-YD 5 "Ares"
-YAa 5 5 66.6      ; EXPECT ^-Aa 5 66\.6
-YjC 5 5 44        ; EXPECT ^-YjC 1 12 .* 44
-Yk0 3 3 Sky       ; EXPECT ^-Yk0 1 7 +Red Mai Sky
-Yk7 3 3 Sky       ; EXPECT ^-Yk7 1 7 +Red Ind Sky
-Yk 3 3 Sky        ; EXPECT ^-Yk  0 8 +Bla Whi LtG Sky

; ---- T4's other half, 2026-09-02 (work log item 172) ----
; Everything above was chosen by hand. This block was measured: the saved
; file was compared against the EXPECT set, and every value switch the
; writer emits with nothing asserting it got a sentinel here. That is the
; gap item 140 fell through -- the -b family was dropped for five days
; because no fixture line set it.
:d 96                   ; EXPECT ^:d 96
:gs 2                   ; EXPECT ^:gs 2
:pC 2.5                 ; EXPECT ^:pC 2\.5
:pd 360.5               ; EXPECT ^:pd 360\.5
:pO Mar                 ; EXPECT ^:pO Mar
:s Lahi                 ; EXPECT ^:s Lahi
:XS 250                 ; EXPECT ^:XS 250
:Xv 2                   ; EXPECT ^:Xv 2
:Xw 640 480             ; EXPECT ^:Xw 640 480
:XI0 33 1               ; EXPECT ^:XI0 33 1
:Xkv Pur                ; EXPECT ^:Xkv Pur
-Ya1                    ; EXPECT ^-Ya1
-Yi1 "fixdir"           ; EXPECT ^-Yi1 "fixdir"
-Yj0 21 11 16 6         ; EXPECT ^-Yj0 21 11 16 6
-Yj7 11 11 11 6 6 6     ; EXPECT ^-Yj7 11 11 11 6 6 6
-YR0 1 1                ; EXPECT ^-YR0 1 1
-YR1 0 0                ; EXPECT ^-YR1 0 0
-YR2 0 0                ; EXPECT ^-YR2 0 0
-YR7 1 0 0 1 0          ; EXPECT ^-YR7 1 0 0 1 0
-YRp 0 0                ; EXPECT ^-YRp 0 0
-YRZ 1 1 1 1            ; EXPECT ^-YRZ 1 1 1 1
-Yw 3.5                 ; EXPECT ^-Yw 3\.5
:YXp0 9in 12in          ; EXPECT ^:YXp0 9in 12in
:YXv 1 30 12            ; EXPECT ^:YXv 1 30 12
:YXx 3                  ; EXPECT ^:YXx 3
-z0 0                   ; EXPECT ^-z0 0
-zf 77F                 ; EXPECT ^-zf 77F
; -zl's saved form follows the zodiac display format set by :sd below,
; which is worth knowing: a display switch changes how a stored SETTING
; is serialized, so this pattern deliberately stops before the format.
-zl 100W00 50N00        ; EXPECT ^-zl 100
-M0 3 "-i fixture"      ; EXPECT ^-M0 3 "-i fixture"
-an                     ; EXPECT ^-an
; :Xb (bitmap file type) has no fixture line on purpose: NSwXb()
; returns tcError when us.fNoWrite is set (switch.cpp:1474), and
; that is exactly the state a settings save runs in, so the value
; the writer emits cannot be set from here. Measured 2026-09-02.
-Yi2 "fixfont"          ; EXPECT ^-Yi2 "fixfont"
-Yi3 "fixsrc"           ; EXPECT ^-Yi3 "fixsrc"
-YkC Pur Ora Pin Cya    ; EXPECT ^-YkC +Pur Ora Pin Cya
:YXG 212121             ; EXPECT ^:YXG 212121
-zj "Fixture Name" "Fixture Place"  ; EXPECT ^-zj "Fixture Name"
:sd                     ; EXPECT ^:sd
; :YXf has no fixture line either: the writer emits the aggregate
; (":YXf #%06x" of gs.nFontAll, io.cpp) while the switch sets one
; component at a time through a sub-letter -- YXft, YXfs and so on --
; so no single line can set what one line saves. Measured 2026-09-02.
