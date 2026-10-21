# Differential test vectors - progression math (Team D, original game as oracle)

Companion data for `spec-differential-results.md`. Every "expected" value below was produced by running the original DX9 executable's own code on the given input (emulated, real shipped bytes). Floats are single precision; each is given as its shortest decimal that converts back to exactly the same single-precision value. "Team B" is what the rebuilt code returned at snapshot 7436a78.


## V1. Respect multiplier scaling: amount x multiplier rounded to an integer

Rule (matches all 7,998 recorded cases): take the product, round it to single precision, then round to the nearest integer with halves away from zero. Results outside the 32-bit range come out as -2147483648.

| multiplier | amount | expected | Team B |
|---|---:|---:|---:|
| 1.05 | 10 | 11 | 10 |
| 1.05 | 150 | 158 | 157 |
| 1.05 | 250 | 263 | 262 |
| 1.05 | -10 | -11 | -10 |
| 1.05 | 17530 | 18407 | 18406 |
| 1.05 | 9670 | 10154 | 10153 |
| 1.15 | 10 | 12 | 11 |
| 1.15 | 30 | 35 | 34 |
| 1.15 | 50 | 58 | 57 |
| 1.15 | 90 | 104 | 103 |
| 1.15 | 150 | 173 | 172 |
| 1.15 | 250 | 288 | 287 |
| 1.15 | -10 | -12 | -11 |
| 1.15 | 14670 | 16871 | 16870 |
| 1.15 | 17530 | 20160 | 20159 |
| 1.15 | 11490 | 13214 | 13213 |
| 1.15 | 450 | 518 | 517 |
| 1.15 | 1890 | 2174 | 2173 |
| 1.15 | 9670 | 11121 | 11120 |
| 1.15 | 11590 | 13329 | 13328 |
| 1.15 | 6130 | 7050 | 7049 |
| 1.3 | 5 | 7 | 6 |
| 1.3 | 15 | 20 | 19 |
| 1.3 | 25 | 33 | 32 |
| 1.3 | 75 | 98 | 97 |
| 1.3 | 125 | 163 | 162 |
| 1.3 | 525 | 683 | 682 |
| 1.3 | -15 | -20 | -19 |
| 1.3 | 17075 | 22198 | 22197 |
| 1.3 | 9875 | 12838 | 12837 |
| 1.3 | 1225 | 1593 | 1592 |
| 1.3 | 12895 | 16764 | 16763 |
| 1.3 | 8155 | 10602 | 10601 |
| 1.3 | 14215 | 18480 | 18479 |
| 1.3 | 10235 | 13306 | 13305 |
| 1.3 | 16635 | 21626 | 21625 |
| 1.3 | 18805 | 24447 | 24446 |
| 1.3 | 2105 | 2737 | 2736 |
| 1.3 | 18835 | 24486 | 24485 |
| 1.55 | 10 | 16 | 15 |
| 1.55 | 30 | 47 | 46 |
| 1.55 | 50 | 78 | 77 |
| 1.55 | 90 | 140 | 139 |
| 1.55 | 150 | 233 | 232 |
| 1.55 | 250 | 388 | 387 |
| 1.55 | -10 | -16 | -15 |
| 1.55 | 14670 | 22739 | 22738 |
| 1.55 | 17530 | 27172 | 27171 |
| 1.55 | 11490 | 17810 | 17809 |
| 1.55 | 450 | 698 | 697 |
| 1.55 | 1890 | 2930 | 2929 |
| 1.55 | 9670 | 14989 | 14988 |
| 1.55 | 11590 | 17965 | 17964 |
| 1.55 | 6130 | 9502 | 9501 |
| 2.7104511 | 18907 | 51247 | 51246 |
| 1.15 | 18805 | 21626 | 21626 |
| 1.05 | 1320 | 1386 | 1386 |
| 1.2 | 16635 | 19962 | 19962 |
| 1.35 | 994 | 1342 | 1342 |
| 1 | 12322 | 12322 | 12322 |
| 1 | 13648 | 13648 | 13648 |
| 1.5 | 16635 | 24953 | 24953 |
| 1.3 | 1216 | 1581 | 1581 |
| 1.05 | 200 | 210 | 210 |
| 1.2 | 16971 | 20365 | 20365 |
| 1.3 | 6099 | 7929 | 7929 |
| 1 | 4526 | 4526 | 4526 |
| 1.25 | 9499 | 11874 | 11874 |
| 1.1 | 8418 | 9260 | 9260 |
| 1 | 2869 | 2869 | 2869 |
| 1 | 8479 | 8479 | 8479 |
| 1.25 | 250 | 313 | 313 |
| 1.2 | 16828 | 20194 | 20194 |
| 1 | 1516 | 1516 | 1516 |
| 1.1 | 11590 | 12749 | 12749 |
| 1.05 | 30 | 31 | 31 |
| 1.3 | 9564 | 12433 | 12433 |
| 1.2 | 8479 | 10175 | 10175 |
| 1 | 1843 | 1843 | 1843 |
| 1.5 | 4944 | 7416 | 7416 |
| 1.3 | 994 | 1292 | 1292 |
| 1.05 | 14558 | 15286 | 15286 |
| 1.1 | 14120 | 15532 | 15532 |
| 1.35 | 11490 | 15512 | 15512 |
| 1.35 | 5372 | 7252 | 7252 |
| 1 | 1734 | 1734 | 1734 |
| 1.3 | 18328 | 23826 | 23826 |
| 1.3 | 10272 | 13354 | 13354 |
| 1.2 | 4081 | 4897 | 4897 |
| 1 | 18637 | 18637 | 18637 |
| 1.1 | 18637 | 20501 | 20501 |
| 1 | 704 | 704 | 704 |
| 1.3 | 7393 | 9611 | 9611 |
| 1.05 | 12322 | 12938 | 12938 |
| 1.15 | 10802 | 12422 | 12422 |
| -1 | 16777217 | -16777216 | -16777217 |
| -1 | 100000001 | -100000000 | -100000001 |
| -1 | 2147483647 | -2147483648 | -2147483647 |
| 0 | 16777217 | 0 | 0 |
| 0 | 100000001 | 0 | 0 |
| 0 | 2147483647 | 0 | 0 |
| 0.5 | 16777217 | 8388608 | 8388609 |
| 0.5 | 100000001 | 50000000 | 50000001 |
| 0.5 | 2147483647 | 1073741824 | 1073741824 |
| 0.74196064 | 16777217 | 12448035 | 12448035 |
| 0.74196064 | 100000001 | 74196064 | 74196065 |
| 0.74196064 | 2147483647 | 1593348352 | 1593348351 |

## V2. Gang-kill respect with a per-kill factor

Table value 10 for every rank. Rule: same rounding as V1 (product rounded to single precision, then halves away from zero). Rank mapping (rank 3 or more = kingpin, 2 = gangsta, 1 = killa, anything else = thug) matched in every case.

| rank | gang member | excluded | per-kill factor | expected | Team B |
|---:|---:|---:|---|---:|---:|
| -1 | 1 | 0 | 1.05 | 11 | 10 |
| -1 | 1 | 0 | 1.15 | 12 | 11 |
| -1 | 1 | 0 | 0.95 | 10 | 9 |
| 0 | 1 | 0 | 1.05 | 11 | 10 |
| 0 | 1 | 0 | 1.15 | 12 | 11 |
| 0 | 1 | 0 | 0.95 | 10 | 9 |
| 1 | 1 | 0 | 1.05 | 11 | 10 |
| 1 | 1 | 0 | 1.15 | 12 | 11 |
| 1 | 1 | 0 | 0.95 | 10 | 9 |
| 2 | 1 | 0 | 1.05 | 11 | 10 |
| 2 | 1 | 0 | 1.15 | 12 | 11 |
| 2 | 1 | 0 | 0.95 | 10 | 9 |
| 3 | 1 | 0 | 1.05 | 11 | 10 |
| 3 | 1 | 0 | 1.15 | 12 | 11 |
| 3 | 1 | 0 | 0.95 | 10 | 9 |
| 4 | 1 | 0 | 1.05 | 11 | 10 |
| 4 | 1 | 0 | 1.15 | 12 | 11 |
| 4 | 1 | 0 | 0.95 | 10 | 9 |
| 99 | 1 | 0 | 1.05 | 11 | 10 |
| 99 | 1 | 0 | 1.15 | 12 | 11 |
| 99 | 1 | 0 | 0.95 | 10 | 9 |
| 2 | 1 | 1 | 1.9288577 | 0 | 0 |
| 0 | 0 | 1 | 2.8593063 | 0 | 0 |
| 4 | 0 | 0 | 0 | 0 | 0 |
| 0 | 0 | 0 | 1.6178455 | 0 | 0 |
| 4 | 0 | 1 | 1.35 | 0 | 0 |
| 1 | 1 | 1 | 2.177429 | 0 | 0 |
| 4 | 0 | 0 | 1.1032574 | 0 | 0 |
| 99 | 0 | 1 | 0.05 | 0 | 0 |
| 0 | 1 | 0 | 1.7211366 | 17 | 17 |
| -1 | 1 | 1 | 2.1552587 | 0 | 0 |
| 4 | 0 | 1 | 1.9288577 | 0 | 0 |
| 4 | 1 | 1 | 1.0072203 | 0 | 0 |
| 0 | 1 | 0 | 2.1031625 | 21 | 21 |
| 2 | 0 | 1 | 0.11358105 | 0 | 0 |
| -1 | 1 | 1 | 2.5353155 | 0 | 0 |
| 4 | 0 | 0 | 1.7666739 | 0 | 0 |
| 99 | 1 | 0 | 0.41334948 | 4 | 4 |
| -1 | 1 | 0 | 0.6358367 | 6 | 6 |
| 4 | 0 | 0 | 0.7997147 | 0 | 0 |
| -1 | 1 | 0 | 0.75 | 8 | 8 |
| 4 | 1 | 0 | 0.3231567 | 3 | 3 |
| 0 | 1 | 1 | 0.058599982 | 0 | 0 |
| 3 | 1 | 0 | 0.41334948 | 4 | 4 |
| 99 | 0 | 1 | 0.25 | 0 | 0 |
| 3 | 1 | 1 | 2.4445896 | 0 | 0 |

## V3. Diversion trick reward (fraction, Max_Respect, Max_Lifetime_Respect, Max_Cash)

Rule (matches all 2,040 recorded cases): f = fraction clamped to [0, 1]. If f is not greater than 0.001 (the single-precision value 0.0010000000474974513) nothing is paid. Lifetime limit not reached: respect = truncate(f x Max_Respect). Reached: cash = floor(f x Max_Cash, the product first rounded to single precision); a result of 0 pays nothing. Output column = "respect cash".

| fraction | Max_Respect | Max_Lifetime | Max_Cash | lifetime reached | expected | Team B |
|---|---:|---:|---:|---:|---|---|
| 0.01 | 30 | 3000 | 80 | 1 | 0 0 | 0 1 |
| 0.3333 | 30 | 3000 | 80 | 1 | 0 26 | 0 27 |
| 0.57 | 30 | 3000 | 80 | 1 | 0 45 | 0 46 |
| 0.999 | 30 | 3000 | 80 | 1 | 0 79 | 0 80 |
| 0.47009966 | 30 | 3000 | 80 | 1 | 0 37 | 0 38 |
| 0.16104017 | 30 | 3000 | 80 | 1 | 0 12 | 0 13 |
| 0.39695024 | 30 | 3000 | 80 | 1 | 0 31 | 0 32 |
| 0.47183704 | 30 | 3000 | 80 | 1 | 0 37 | 0 38 |
| 0.6708467 | 30 | 3000 | 80 | 1 | 0 53 | 0 54 |
| 0.6838206 | 30 | 3000 | 80 | 1 | 0 54 | 0 55 |
| 0.13299817 | 30 | 3000 | 80 | 1 | 0 10 | 0 11 |
| 0.63514876 | 30 | 3000 | 80 | 1 | 0 50 | 0 51 |
| 0.3971363 | 30 | 3000 | 80 | 1 | 0 31 | 0 32 |
| 0.1729582 | 30 | 3000 | 80 | 1 | 0 13 | 0 14 |
| 0.769622 | 30 | 3000 | 80 | 1 | 0 61 | 0 62 |
| 0.34909487 | 30 | 3000 | 80 | 1 | 0 27 | 0 28 |
| 0.4844515 | 30 | 3000 | 80 | 1 | 0 38 | 0 39 |
| 0.048415244 | 30 | 3000 | 80 | 1 | 0 3 | 0 4 |
| 0.8952497 | 30 | 3000 | 80 | 1 | 0 71 | 0 72 |
| 0.34589237 | 30 | 3000 | 80 | 1 | 0 27 | 0 28 |
| 0.498285 | 30 | 3000 | 80 | 1 | 0 39 | 0 40 |
| 0.45943427 | 30 | 3000 | 80 | 1 | 0 36 | 0 37 |
| 0.51198786 | 30 | 3000 | 80 | 1 | 0 40 | 0 41 |
| 0.93297344 | 30 | 3000 | 80 | 1 | 0 74 | 0 75 |
| 0.6840213 | 30 | 3000 | 80 | 1 | 0 54 | 0 55 |
| 0.61883163 | 30 | 3000 | 80 | 1 | 0 49 | 0 50 |
| 0.92420423 | 30 | 3000 | 80 | 1 | 0 73 | 0 74 |
| 0.29402697 | 30 | 3000 | 80 | 1 | 0 23 | 0 24 |
| 0.30828774 | 30 | 3000 | 80 | 1 | 0 24 | 0 25 |
| 0.19414665 | 30 | 3000 | 80 | 1 | 0 15 | 0 16 |
| 0.44861054 | 30 | 3000 | 80 | 1 | 0 35 | 0 36 |
| 0.8238975 | 30 | 3000 | 80 | 1 | 0 65 | 0 66 |
| 0.6463019 | 30 | 3000 | 80 | 1 | 0 51 | 0 52 |
| 0.8108811 | 30 | 3000 | 80 | 1 | 0 64 | 0 65 |
| 0.747725 | 30 | 3000 | 80 | 1 | 0 59 | 0 60 |
| 0.9623784 | 30 | 3000 | 80 | 1 | 0 76 | 0 77 |
| 0.081364855 | 30 | 3000 | 80 | 1 | 0 6 | 0 7 |
| 0.7973388 | 30 | 3000 | 80 | 1 | 0 63 | 0 64 |
| 0.8618537 | 30 | 3000 | 80 | 1 | 0 68 | 0 69 |
| 0.37378633 | 30 | 3000 | 80 | 1 | 0 29 | 0 30 |
| 0.34674054 | 30 | 3000 | 80 | 1 | 0 27 | 0 28 |
| 0.8360545 | 30 | 3000 | 80 | 1 | 0 66 | 0 67 |
| 0.5955074 | 30 | 3000 | 80 | 1 | 0 47 | 0 48 |
| 0.5098942 | 30 | 3000 | 80 | 1 | 0 40 | 0 41 |
| 0.999 | 25 | 10000 | 100 | 1 | 0 99 | 0 100 |
| 0.39695024 | 25 | 10000 | 100 | 1 | 0 39 | 0 40 |
| 0.5783864 | 25 | 10000 | 100 | 1 | 0 57 | 0 58 |
| 0.63514876 | 25 | 10000 | 100 | 1 | 0 63 | 0 64 |
| 0.3971363 | 25 | 10000 | 100 | 1 | 0 39 | 0 40 |
| 0.66753155 | 25 | 10000 | 100 | 1 | 0 66 | 0 67 |
| 0.769622 | 25 | 10000 | 100 | 1 | 0 76 | 0 77 |
| 0.34909487 | 25 | 10000 | 100 | 1 | 0 34 | 0 35 |
| 0.048415244 | 25 | 10000 | 100 | 1 | 0 4 | 0 5 |
| 0.8952497 | 25 | 10000 | 100 | 1 | 0 89 | 0 90 |
| 0.025060879 | 25 | 10000 | 100 | 1 | 0 2 | 0 3 |
| 0.34589237 | 25 | 10000 | 100 | 1 | 0 34 | 0 35 |
| 0.498285 | 25 | 10000 | 100 | 1 | 0 49 | 0 50 |
| 0.016665395 | 25 | 10000 | 100 | 1 | 0 1 | 0 2 |
| 0.45943427 | 25 | 10000 | 100 | 1 | 0 45 | 0 46 |
| 0.2152818 | 25 | 10000 | 100 | 1 | 0 21 | 0 22 |
| 0.61883163 | 25 | 10000 | 100 | 1 | 0 61 | 0 62 |
| 0.77769846 | 25 | 10000 | 100 | 1 | 0 77 | 0 78 |
| 0.30828774 | 25 | 10000 | 100 | 1 | 0 30 | 0 31 |
| 0.44861054 | 25 | 10000 | 100 | 1 | 0 44 | 0 45 |
| 0.6463019 | 25 | 10000 | 100 | 1 | 0 64 | 0 65 |
| 0.747725 | 25 | 10000 | 100 | 1 | 0 74 | 0 75 |
| 0.48992154 | 25 | 10000 | 100 | 1 | 0 48 | 0 49 |
| 0.7973388 | 25 | 10000 | 100 | 1 | 0 79 | 0 80 |
| 0.9660962 | 25 | 10000 | 100 | 1 | 0 96 | 0 97 |
| 0.34674054 | 25 | 10000 | 100 | 1 | 0 34 | 0 35 |
| 0.47965533 | 25 | 10000 | 100 | 1 | 0 47 | 0 48 |
| 0.8383527 | 25 | 10000 | 100 | 1 | 0 83 | 0 84 |
| 0.8360545 | 25 | 10000 | 100 | 1 | 0 83 | 0 84 |
| 0.5955074 | 25 | 10000 | 100 | 1 | 0 59 | 0 60 |
| 0.5098942 | 25 | 10000 | 100 | 1 | 0 50 | 0 51 |
| 0.61870456 | 25 | 10000 | 100 | 1 | 0 61 | 0 62 |
| 0.999 | 30 | 10000 | 100 | 1 | 0 99 | 0 100 |
| 0.39695024 | 30 | 10000 | 100 | 1 | 0 39 | 0 40 |
| 0.5783864 | 30 | 10000 | 100 | 1 | 0 57 | 0 58 |
| 0.63514876 | 30 | 10000 | 100 | 1 | 0 63 | 0 64 |
| 0.3971363 | 30 | 10000 | 100 | 1 | 0 39 | 0 40 |
| 0.66753155 | 30 | 10000 | 100 | 1 | 0 66 | 0 67 |
| 0.769622 | 30 | 10000 | 100 | 1 | 0 76 | 0 77 |
| 0.34909487 | 30 | 10000 | 100 | 1 | 0 34 | 0 35 |
| 0.048415244 | 30 | 10000 | 100 | 1 | 0 4 | 0 5 |
| 0.8952497 | 30 | 10000 | 100 | 1 | 0 89 | 0 90 |
| 0.025060879 | 30 | 10000 | 100 | 1 | 0 2 | 0 3 |
| 0.34589237 | 30 | 10000 | 100 | 1 | 0 34 | 0 35 |
| 0.498285 | 30 | 10000 | 100 | 1 | 0 49 | 0 50 |
| 0.016665395 | 30 | 10000 | 100 | 1 | 0 1 | 0 2 |
| 0.45943427 | 30 | 10000 | 100 | 1 | 0 45 | 0 46 |
| 0.2152818 | 30 | 10000 | 100 | 1 | 0 21 | 0 22 |
| 0.61883163 | 30 | 10000 | 100 | 1 | 0 61 | 0 62 |
| 0.77769846 | 30 | 10000 | 100 | 1 | 0 77 | 0 78 |
| 0.30828774 | 30 | 10000 | 100 | 1 | 0 30 | 0 31 |
| 0.44861054 | 30 | 10000 | 100 | 1 | 0 44 | 0 45 |
| 0.6463019 | 30 | 10000 | 100 | 1 | 0 64 | 0 65 |
| 0.747725 | 30 | 10000 | 100 | 1 | 0 74 | 0 75 |
| 0.48992154 | 30 | 10000 | 100 | 1 | 0 48 | 0 49 |
| 0.7973388 | 30 | 10000 | 100 | 1 | 0 79 | 0 80 |
| 0.9660962 | 30 | 10000 | 100 | 1 | 0 96 | 0 97 |
| 0.34674054 | 30 | 10000 | 100 | 1 | 0 34 | 0 35 |
| 0.47965533 | 30 | 10000 | 100 | 1 | 0 47 | 0 48 |
| 0.8383527 | 30 | 10000 | 100 | 1 | 0 83 | 0 84 |
| 0.8360545 | 30 | 10000 | 100 | 1 | 0 83 | 0 84 |
| 0.5955074 | 30 | 10000 | 100 | 1 | 0 59 | 0 60 |
| 0.5098942 | 30 | 10000 | 100 | 1 | 0 50 | 0 51 |
| 0.61870456 | 30 | 10000 | 100 | 1 | 0 61 | 0 62 |
| 0.999 | 40 | 5000 | 100 | 1 | 0 99 | 0 100 |
| 0.39695024 | 40 | 5000 | 100 | 1 | 0 39 | 0 40 |
| 0.66753155 | 125 | 10000 | 250 | 0 | 83 0 | 83 0 |
| 0.081364855 | 1 | 1 | 1 | 0 | 0 0 | 0 0 |
| 0.8108811 | 10 | 1000 | 10 | 0 | 8 0 | 8 0 |
| 2e-06 | 150 | 8000 | 150 | 1 | 0 0 | 0 0 |
| 0.34674054 | 50 | 4000 | 200 | 1 | 0 69 | 0 69 |
| 0.37378633 | 125 | 10000 | 250 | 0 | 46 0 | 46 0 |
| 0.5783864 | 75 | 5000 | 100 | 0 | 43 0 | 43 0 |
| 0.34589237 | 10 | 1000 | 10 | 1 | 0 3 | 0 3 |
| 0.19414665 | 40 | 5000 | 100 | 1 | 0 19 | 0 19 |
| 0.30828774 | 30 | 10000 | 100 | 0 | 9 0 | 9 0 |
| 0.2152818 | 10000 | 100 | 10000 | 0 | 2152 0 | 2152 0 |
| 0.37378633 | 1 | 1 | 1 | 0 | 0 0 | 0 0 |
| 0.6840213 | 40 | 5000 | 100 | 1 | 0 68 | 0 68 |
| 0.99 | 25 | 10000 | 100 | 1 | 0 99 | 0 99 |
| 0.30828774 | 50 | 4000 | 200 | 0 | 15 0 | 15 0 |
| 0.498285 | 10 | 1000 | 10 | 0 | 4 0 | 4 0 |
| 0.5098942 | 150 | 8000 | 150 | 0 | 76 0 | 76 0 |
| 0.34909487 | 150 | 8000 | 150 | 0 | 52 0 | 52 0 |
| 0.3333 | 75 | 5000 | 100 | 1 | 0 33 | 0 33 |
| 0.001 | 1 | 1 | 1 | 1 | 0 0 | 0 0 |
| 0.34363344 | 125 | 10000 | 250 | 0 | 42 0 | 42 0 |
| 0.7934457 | 10 | 1000 | 10 | 0 | 7 0 | 7 0 |
| 0.6 | 50 | 6000 | 200 | 1 | 0 120 | 0 120 |
| 0.01 | 25 | 10000 | 100 | 1 | 0 1 | 0 1 |
| 0.9623784 | 25 | 10000 | 100 | 1 | 0 96 | 0 96 |
| 0.747725 | 150 | 8000 | 150 | 0 | 112 0 | 112 0 |
| 0.16104017 | 125 | 10000 | 250 | 0 | 20 0 | 20 0 |
| 0.66753155 | 30 | 10000 | 100 | 0 | 20 0 | 20 0 |
| 0.4844515 | 1 | 1 | 1 | 0 | 0 0 | 0 0 |
| 0.25 | 75 | 5000 | 100 | 0 | 18 0 | 18 0 |

## V4. Reward-toast finalisation (respect field, cash-dollars field, multiplier, player state)

Player state is (total, in-level, internal level L) with the shipped respect table. Output = "granted total in-level L, overflowCash = raw cash paid by the award's at-cap overflow, toastCash = raw cash paid by the toast's own cash field, zeroHint = 1 when the nothing-granted notification is posted".

| state | toast respect | toast cash $ | multiplier | expected | Team B |
|---|---:|---|---|---|---|
| (238875, 0, 49) | 25 | 21474836 | 1.05 | 0 238875 0 49 overflowCash=2600 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=2600 toastCash=2000000000 zeroHint=1 |
| (238875, 0, 49) | 150 | 3e+07 | 1.35 | 0 238875 0 49 overflowCash=20300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=20300 toastCash=2000000000 zeroHint=1 |
| (238870, 7630, 48) | -5 | -1 | 1.35 | 0 238870 7630 48 overflowCash=0 toastCash=0 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=-100 zeroHint=1 |
| (238875, 0, 49) | 10 | 1e+03 | 1 | 0 238875 0 49 overflowCash=1000 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=1000 toastCash=100000 zeroHint=1 |
| (238870, 7630, 48) | 150 | 1e+03 | 1 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=100000 zeroHint=0 |
| (238870, 7630, 48) | 25 | 3e+03 | 1.45 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=300000 zeroHint=0 |
| (238875, 0, 49) | 75 | 1e+01 | 1.45 | 0 238875 0 49 overflowCash=10900 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=10900 toastCash=1000 zeroHint=1 |
| (238875, 0, 49) | 0 | 1 | 1.25 | 0 238875 0 49 overflowCash=0 toastCash=100 zeroHint=0 | 0 238875 0 49 overflowCash=0 toastCash=100 zeroHint=1 |
| (238875, 0, 49) | 1 | 0.333 | 1.25 | 0 238875 0 49 overflowCash=100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=100 toastCash=33 zeroHint=1 |
| (6600, 2255, 2) | 10 | 21474836 | 1.25 | 13 6613 2268 2 overflowCash=0 toastCash=0 zeroHint=0 | 13 6613 2268 2 overflowCash=0 toastCash=2000000000 zeroHint=0 |
| (238875, 0, 49) | 250 | 1e+02 | 1.45 | 0 238875 0 49 overflowCash=36300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=36300 toastCash=10000 zeroHint=1 |
| (6600, 2255, 2) | 250 | 0.333 | 1.35 | 338 6938 248 3 overflowCash=0 toastCash=0 zeroHint=0 | 338 6938 248 3 overflowCash=0 toastCash=33 zeroHint=0 |
| (238870, 7630, 48) | 150 | -1 | 1.45 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=-100 zeroHint=0 |
| (238870, 7630, 48) | 25 | 79 | 1.35 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=7900 zeroHint=0 |
| (238870, 7630, 48) | 75 | 3e+03 | 1 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=300000 zeroHint=0 |
| (238870, 7630, 48) | 1000 | 1e+01 | 1 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=1000 zeroHint=0 |
| (238875, 0, 49) | 75 | 0.125 | 1.15 | 0 238875 0 49 overflowCash=8600 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=8600 toastCash=13 zeroHint=1 |
| (6600, 2255, 2) | 75 | 12.345 | 1.35 | 101 6701 11 3 overflowCash=0 toastCash=0 zeroHint=0 | 101 6701 11 3 overflowCash=0 toastCash=1235 zeroHint=0 |
| (238870, 7630, 48) | 75 | 1e+01 | 1 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=1000 zeroHint=0 |
| (238870, 7630, 48) | 30 | 21474836 | 1 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=2000000000 zeroHint=0 |
| (238870, 7630, 48) | 0 | 0 | 1.25 | 0 238870 7630 48 overflowCash=0 toastCash=0 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=0 zeroHint=1 |
| (6600, 2255, 2) | 75 | 21474836 | 1.45 | 109 6709 19 3 overflowCash=0 toastCash=0 zeroHint=0 | 109 6709 19 3 overflowCash=0 toastCash=2000000000 zeroHint=0 |
| (238875, 0, 49) | 10 | 12.345 | 1.35 | 0 238875 0 49 overflowCash=1400 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=1400 toastCash=1235 zeroHint=1 |
| (238875, 0, 49) | 150 | 1e+02 | 1.15 | 0 238875 0 49 overflowCash=17300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=17200 toastCash=10000 zeroHint=1 |
| (238870, 7630, 48) | 150 | 1e+02 | 1.25 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=10000 zeroHint=0 |
| (238870, 7630, 48) | 10 | 79 | 1.25 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=7900 zeroHint=0 |
| (238870, 7630, 48) | -5 | 21474836 | 1 | 0 238870 7630 48 overflowCash=0 toastCash=-2000000000 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=2000000000 zeroHint=1 |
| (238875, 0, 49) | 30 | 0.015 | 1.35 | 0 238875 0 49 overflowCash=4100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=4100 toastCash=1 zeroHint=1 |
| (6600, 2255, 2) | 25 | 12.345 | 1.25 | 31 6631 2286 2 overflowCash=0 toastCash=0 zeroHint=0 | 31 6631 2286 2 overflowCash=0 toastCash=1235 zeroHint=0 |
| (238875, 0, 49) | 75 | 1e+02 | 1.05 | 0 238875 0 49 overflowCash=7900 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=7900 toastCash=10000 zeroHint=1 |
| (238870, 7630, 48) | -5 | 1e+01 | 1.05 | 0 238870 7630 48 overflowCash=0 toastCash=1000 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=1000 zeroHint=1 |
| (6600, 2255, 2) | 250 | 79 | 1.35 | 338 6938 248 3 overflowCash=0 toastCash=0 zeroHint=0 | 338 6938 248 3 overflowCash=0 toastCash=7900 zeroHint=0 |
| (238875, 0, 49) | 30 | 3e+03 | 1 | 0 238875 0 49 overflowCash=3000 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=3000 toastCash=300000 zeroHint=1 |
| (238870, 7630, 48) | -5 | 0.005 | 1.15 | 0 238870 7630 48 overflowCash=0 toastCash=1 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=0 zeroHint=1 |
| (238875, 0, 49) | 150 | 21474836 | 1.15 | 0 238875 0 49 overflowCash=17300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=17200 toastCash=2000000000 zeroHint=1 |
| (238875, 0, 49) | 1 | 1e+03 | 1.05 | 0 238875 0 49 overflowCash=100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=100 toastCash=100000 zeroHint=1 |
| (6600, 2255, 2) | 25 | -1 | 1.15 | 29 6629 2284 2 overflowCash=0 toastCash=0 zeroHint=0 | 29 6629 2284 2 overflowCash=0 toastCash=-100 zeroHint=0 |
| (6600, 2255, 2) | 250 | 21474836 | 1.25 | 313 6913 223 3 overflowCash=0 toastCash=0 zeroHint=0 | 313 6913 223 3 overflowCash=0 toastCash=2000000000 zeroHint=0 |
| (238870, 7630, 48) | 250 | 0.015 | 1.25 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=1 zeroHint=0 |
| (238870, 7630, 48) | 250 | 3e+03 | 1.35 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=300000 zeroHint=0 |
| (6600, 2255, 2) | 250 | -1 | 1 | 250 6850 160 3 overflowCash=0 toastCash=0 zeroHint=0 | 250 6850 160 3 overflowCash=0 toastCash=-100 zeroHint=0 |
| (6600, 2255, 2) | 75 | 12.345 | 1.05 | 79 6679 2334 2 overflowCash=0 toastCash=0 zeroHint=0 | 79 6679 2334 2 overflowCash=0 toastCash=1235 zeroHint=0 |
| (6600, 2255, 2) | 10 | 0.333 | 1.35 | 14 6614 2269 2 overflowCash=0 toastCash=0 zeroHint=0 | 14 6614 2269 2 overflowCash=0 toastCash=33 zeroHint=0 |
| (238875, 0, 49) | 1000 | 1 | 1.25 | 0 238875 0 49 overflowCash=125000 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=125000 toastCash=100 zeroHint=1 |
| (6600, 2255, 2) | 10 | 21474836 | 1.45 | 15 6615 2270 2 overflowCash=0 toastCash=0 zeroHint=0 | 15 6615 2270 2 overflowCash=0 toastCash=2000000000 zeroHint=0 |
| (238875, 0, 49) | 150 | 0 | 1.15 | 0 238875 0 49 overflowCash=17300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=17200 toastCash=0 zeroHint=1 |
| (238870, 7630, 48) | 10 | 1 | 1.05 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=100 zeroHint=0 |
| (238870, 7630, 48) | -5 | 0.333 | 1.35 | 0 238870 7630 48 overflowCash=0 toastCash=33 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=33 zeroHint=1 |
| (6600, 2255, 2) | 10 | 3e+07 | 1.45 | 15 6615 2270 2 overflowCash=0 toastCash=0 zeroHint=0 | 15 6615 2270 2 overflowCash=0 toastCash=2000000000 zeroHint=0 |
| (238875, 0, 49) | 150 | -1 | 1.15 | 0 238875 0 49 overflowCash=17300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=17200 toastCash=-100 zeroHint=1 |
| (6600, 2255, 2) | 0 | 79 | 1.35 | 0 6600 2255 2 overflowCash=0 toastCash=7900 zeroHint=0 | 0 6600 2255 2 overflowCash=0 toastCash=7900 zeroHint=1 |
| (238870, 7630, 48) | 25 | 3e+03 | 1.25 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=300000 zeroHint=0 |
| (238875, 0, 49) | 150 | -1 | 1.25 | 0 238875 0 49 overflowCash=18800 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=18800 toastCash=-100 zeroHint=1 |
| (6600, 2255, 2) | 1 | 12.345 | 1.15 | 1 6601 2256 2 overflowCash=0 toastCash=0 zeroHint=0 | 1 6601 2256 2 overflowCash=0 toastCash=1235 zeroHint=0 |
| (6600, 2255, 2) | 25 | 0.333 | 1.05 | 26 6626 2281 2 overflowCash=0 toastCash=0 zeroHint=0 | 26 6626 2281 2 overflowCash=0 toastCash=33 zeroHint=0 |
| (6600, 2255, 2) | 250 | 1 | 1 | 250 6850 160 3 overflowCash=0 toastCash=0 zeroHint=0 | 250 6850 160 3 overflowCash=0 toastCash=100 zeroHint=0 |
| (238875, 0, 49) | 150 | 1 | 1.05 | 0 238875 0 49 overflowCash=15800 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=15700 toastCash=100 zeroHint=1 |
| (238875, 0, 49) | 25 | 0.333 | 1.05 | 0 238875 0 49 overflowCash=2600 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=2600 toastCash=33 zeroHint=1 |
| (238870, 7630, 48) | 1 | 79 | 1.15 | 1 238871 7631 48 overflowCash=0 toastCash=0 zeroHint=0 | 1 238871 7631 48 overflowCash=0 toastCash=7900 zeroHint=0 |
| (238875, 0, 49) | 1 | 1e+03 | 1.15 | 0 238875 0 49 overflowCash=100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=100 toastCash=100000 zeroHint=1 |
| (238875, 0, 49) | -5 | 0 | 1 | 0 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 0 238875 0 49 overflowCash=0 toastCash=0 zeroHint=1 |
| (6600, 2255, 2) | 150 | 1e+02 | 1.35 | 203 6803 113 3 overflowCash=0 toastCash=0 zeroHint=0 | 203 6803 113 3 overflowCash=0 toastCash=10000 zeroHint=0 |
| (238875, 0, 49) | 250 | -1 | 1.45 | 0 238875 0 49 overflowCash=36300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=36300 toastCash=-100 zeroHint=1 |
| (238870, 7630, 48) | 150 | 21474836 | 1.05 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=2000000000 zeroHint=0 |
| (238870, 7630, 48) | 30 | 12.345 | 1.15 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=1235 zeroHint=0 |
| (238870, 7630, 48) | 150 | 1 | 1.15 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=100 zeroHint=0 |
| (238875, 0, 49) | 0 | 0.125 | 1 | 0 238875 0 49 overflowCash=0 toastCash=13 zeroHint=0 | 0 238875 0 49 overflowCash=0 toastCash=13 zeroHint=1 |
| (238870, 7630, 48) | 25 | 0.125 | 1.05 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=13 zeroHint=0 |
| (6600, 2255, 2) | 0 | 21474836 | 1.35 | 0 6600 2255 2 overflowCash=0 toastCash=-2000000000 zeroHint=0 | 0 6600 2255 2 overflowCash=0 toastCash=2000000000 zeroHint=1 |
| (238870, 7630, 48) | 1 | 1 | 1.45 | 1 238871 7631 48 overflowCash=0 toastCash=0 zeroHint=0 | 1 238871 7631 48 overflowCash=0 toastCash=100 zeroHint=0 |
| (6600, 2255, 2) | 30 | 21474836 | 1.25 | 38 6638 2293 2 overflowCash=0 toastCash=0 zeroHint=0 | 38 6638 2293 2 overflowCash=0 toastCash=2000000000 zeroHint=0 |
| (6600, 2255, 2) | 30 | 1e+03 | 1.45 | 44 6644 2299 2 overflowCash=0 toastCash=0 zeroHint=0 | 44 6644 2299 2 overflowCash=0 toastCash=100000 zeroHint=0 |
| (6600, 2255, 2) | 0 | 0.333 | 1.25 | 0 6600 2255 2 overflowCash=0 toastCash=33 zeroHint=0 | 0 6600 2255 2 overflowCash=0 toastCash=33 zeroHint=1 |
| (238875, 0, 49) | 1 | 1 | 1.05 | 0 238875 0 49 overflowCash=100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=100 toastCash=100 zeroHint=1 |
| (238870, 7630, 48) | 0 | 79 | 1.45 | 0 238870 7630 48 overflowCash=0 toastCash=7900 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=7900 zeroHint=1 |
| (238870, 7630, 48) | 30 | 0.125 | 1 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=13 zeroHint=0 |
| (238875, 0, 49) | 25 | 1e+02 | 1.35 | 0 238875 0 49 overflowCash=3400 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=3400 toastCash=10000 zeroHint=1 |
| (238875, 0, 49) | 25 | 21474836 | 1.45 | 0 238875 0 49 overflowCash=3600 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=3600 toastCash=2000000000 zeroHint=1 |
| (238870, 7630, 48) | 250 | 1 | 1.25 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=100 zeroHint=0 |
| (238875, 0, 49) | 250 | 21474836 | 1.25 | 0 238875 0 49 overflowCash=31300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=31300 toastCash=2000000000 zeroHint=1 |
| (238870, 7630, 48) | 250 | 3e+07 | 1.25 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=2000000000 zeroHint=0 |
| (6600, 2255, 2) | 250 | 79 | 1.25 | 313 6913 223 3 overflowCash=0 toastCash=0 zeroHint=0 | 313 6913 223 3 overflowCash=0 toastCash=7900 zeroHint=0 |
| (238875, 0, 49) | 0 | 0.125 | 1.05 | 0 238875 0 49 overflowCash=0 toastCash=13 zeroHint=0 | 0 238875 0 49 overflowCash=0 toastCash=13 zeroHint=1 |
| (238875, 0, 49) | 150 | 0.125 | 1.35 | 0 238875 0 49 overflowCash=20300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=20300 toastCash=13 zeroHint=1 |
| (238870, 7630, 48) | 1000 | 0.015 | 1.25 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=1 zeroHint=0 |
| (238870, 7630, 48) | 75 | -1 | 1 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=-100 zeroHint=0 |
| (6600, 2255, 2) | 250 | 1e+03 | 1.05 | 263 6863 173 3 overflowCash=0 toastCash=0 zeroHint=0 | 262 6862 172 3 overflowCash=0 toastCash=100000 zeroHint=0 |
| (238870, 7630, 48) | 75 | 1e+02 | 1 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=10000 zeroHint=0 |
| (6600, 2255, 2) | 0 | 1e+01 | 1.15 | 0 6600 2255 2 overflowCash=0 toastCash=1000 zeroHint=0 | 0 6600 2255 2 overflowCash=0 toastCash=1000 zeroHint=1 |
| (238875, 0, 49) | 1 | 21474836 | 1.35 | 0 238875 0 49 overflowCash=100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=100 toastCash=2000000000 zeroHint=1 |
| (238875, 0, 49) | 30 | 3e+07 | 1.05 | 0 238875 0 49 overflowCash=3100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=3100 toastCash=2000000000 zeroHint=1 |
| (6600, 2255, 2) | 0 | 21474836 | 1.05 | 0 6600 2255 2 overflowCash=0 toastCash=-2000000000 zeroHint=0 | 0 6600 2255 2 overflowCash=0 toastCash=2000000000 zeroHint=1 |
| (6600, 2255, 2) | 1 | 0.333 | 1.25 | 1 6601 2256 2 overflowCash=0 toastCash=0 zeroHint=0 | 1 6601 2256 2 overflowCash=0 toastCash=33 zeroHint=0 |
| (6600, 2255, 2) | 1000 | 1 | 1.45 | 1450 8050 1360 3 overflowCash=0 toastCash=0 zeroHint=0 | 1450 8050 1360 3 overflowCash=0 toastCash=100 zeroHint=0 |
| (238870, 7630, 48) | 30 | -1 | 1 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=-100 zeroHint=0 |
| (238870, 7630, 48) | 0 | 0.005 | 1.05 | 0 238870 7630 48 overflowCash=0 toastCash=1 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=0 zeroHint=1 |
| (6600, 2255, 2) | 0 | 0 | 1 | 0 6600 2255 2 overflowCash=0 toastCash=0 zeroHint=0 | 0 6600 2255 2 overflowCash=0 toastCash=0 zeroHint=1 |
| (238870, 7630, 48) | 30 | 1e+03 | 1.45 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=100000 zeroHint=0 |
| (238875, 0, 49) | 1 | 79 | 1.45 | 0 238875 0 49 overflowCash=100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=100 toastCash=7900 zeroHint=1 |
| (238875, 0, 49) | 0 | 0.004 | 1.05 | 0 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 0 238875 0 49 overflowCash=0 toastCash=0 zeroHint=1 |
| (238875, 0, 49) | 30 | 1 | 1.25 | 0 238875 0 49 overflowCash=3800 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=3800 toastCash=100 zeroHint=1 |
| (238870, 7630, 48) | 1 | 0.125 | 1 | 1 238871 7631 48 overflowCash=0 toastCash=0 zeroHint=0 | 1 238871 7631 48 overflowCash=0 toastCash=13 zeroHint=0 |
| (6600, 2255, 2) | 75 | 3e+07 | 1 | 75 6675 2330 2 overflowCash=0 toastCash=0 zeroHint=0 | 75 6675 2330 2 overflowCash=0 toastCash=2000000000 zeroHint=0 |
| (238870, 7630, 48) | 0 | 0.125 | 1.15 | 0 238870 7630 48 overflowCash=0 toastCash=13 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=13 zeroHint=1 |
| (238870, 7630, 48) | 75 | 1e+02 | 1.25 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=10000 zeroHint=0 |
| (238870, 7630, 48) | 25 | 79 | 1.05 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=7900 zeroHint=0 |
| (238875, 0, 49) | 25 | 79 | 1.15 | 0 238875 0 49 overflowCash=2900 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=2900 toastCash=7900 zeroHint=1 |
| (6600, 2255, 2) | 10 | 1e+02 | 1.25 | 13 6613 2268 2 overflowCash=0 toastCash=0 zeroHint=0 | 13 6613 2268 2 overflowCash=0 toastCash=10000 zeroHint=0 |
| (6600, 2255, 2) | 1000 | 12.345 | 1.45 | 1450 8050 1360 3 overflowCash=0 toastCash=0 zeroHint=0 | 1450 8050 1360 3 overflowCash=0 toastCash=1235 zeroHint=0 |
| (6600, 2255, 2) | 30 | 1e+01 | 1.25 | 38 6638 2293 2 overflowCash=0 toastCash=0 zeroHint=0 | 38 6638 2293 2 overflowCash=0 toastCash=1000 zeroHint=0 |
| (238875, 0, 49) | 75 | 12.345 | 1.35 | 0 238875 0 49 overflowCash=10100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=10100 toastCash=1235 zeroHint=1 |
| (238870, 7630, 48) | -5 | 3e+07 | 1.05 | 0 238870 7630 48 overflowCash=0 toastCash=-2000000000 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=2000000000 zeroHint=1 |
| (238875, 0, 49) | -5 | 0.125 | 1.05 | 0 238875 0 49 overflowCash=0 toastCash=13 zeroHint=0 | 0 238875 0 49 overflowCash=0 toastCash=13 zeroHint=1 |
| (238875, 0, 49) | 150 | 21474836 | 1.35 | 0 238875 0 49 overflowCash=20300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=20300 toastCash=2000000000 zeroHint=1 |
| (6600, 2255, 2) | 30 | 1e+02 | 1.35 | 41 6641 2296 2 overflowCash=0 toastCash=0 zeroHint=0 | 41 6641 2296 2 overflowCash=0 toastCash=10000 zeroHint=0 |
| (238870, 7630, 48) | 150 | -1 | 1.35 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=-100 zeroHint=0 |
| (238875, 0, 49) | 150 | 12.345 | 1.35 | 0 238875 0 49 overflowCash=20300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=20300 toastCash=1235 zeroHint=1 |
| (238870, 7630, 48) | 0 | 0.005 | 1.25 | 0 238870 7630 48 overflowCash=0 toastCash=1 zeroHint=0 | 0 238870 7630 48 overflowCash=0 toastCash=0 zeroHint=1 |
| (6600, 2255, 2) | 250 | 1e+02 | 1 | 250 6850 160 3 overflowCash=0 toastCash=0 zeroHint=0 | 250 6850 160 3 overflowCash=0 toastCash=10000 zeroHint=0 |
| (238870, 7630, 48) | 25 | -1 | 1.05 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=-100 zeroHint=0 |
| (238875, 0, 49) | 150 | 0.005 | 1 | 0 238875 0 49 overflowCash=15000 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=15000 toastCash=0 zeroHint=1 |
| (238870, 7630, 48) | 1000 | 0 | 1.45 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (238875, 0, 49) | 1 | 0 | 1.25 | 0 238875 0 49 overflowCash=100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=100 toastCash=0 zeroHint=1 |
| (238875, 0, 49) | 250 | 0.005 | 1.45 | 0 238875 0 49 overflowCash=36300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=36300 toastCash=0 zeroHint=1 |
| (238870, 7630, 48) | 10 | 0 | 1.15 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (238870, 7630, 48) | 1000 | 0.005 | 1.25 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (6600, 2255, 2) | 75 | 0 | 1.45 | 109 6709 19 3 overflowCash=0 toastCash=0 zeroHint=0 | 109 6709 19 3 overflowCash=0 toastCash=0 zeroHint=0 |
| (238870, 7630, 48) | 10 | 0.004 | 1.05 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (6600, 2255, 2) | 25 | 0 | 1.35 | 34 6634 2289 2 overflowCash=0 toastCash=0 zeroHint=0 | 34 6634 2289 2 overflowCash=0 toastCash=0 zeroHint=0 |
| (238870, 7630, 48) | 10 | 0 | 1 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (6600, 2255, 2) | 75 | 0.004 | 1.05 | 79 6679 2334 2 overflowCash=0 toastCash=0 zeroHint=0 | 79 6679 2334 2 overflowCash=0 toastCash=0 zeroHint=0 |
| (6600, 2255, 2) | 25 | 0.005 | 1.35 | 34 6634 2289 2 overflowCash=0 toastCash=0 zeroHint=0 | 34 6634 2289 2 overflowCash=0 toastCash=0 zeroHint=0 |
| (238870, 7630, 48) | 10 | 0 | 1.25 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (6600, 2255, 2) | 1 | 0.004 | 1.05 | 1 6601 2256 2 overflowCash=0 toastCash=0 zeroHint=0 | 1 6601 2256 2 overflowCash=0 toastCash=0 zeroHint=0 |
| (238870, 7630, 48) | 30 | 0.005 | 1.45 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (238870, 7630, 48) | 30 | 0.004 | 1.35 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (238875, 0, 49) | 10 | 0.004 | 1 | 0 238875 0 49 overflowCash=1000 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=1000 toastCash=0 zeroHint=1 |
| (238875, 0, 49) | 1000 | 0.005 | 1.45 | 0 238875 0 49 overflowCash=145000 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=145000 toastCash=0 zeroHint=1 |
| (238870, 7630, 48) | 75 | 0 | 1.15 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (238870, 7630, 48) | 10 | 0 | 1.45 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (238875, 0, 49) | 150 | 0 | 1 | 0 238875 0 49 overflowCash=15000 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=15000 toastCash=0 zeroHint=1 |
| (238870, 7630, 48) | 75 | 0 | 1.45 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (238875, 0, 49) | 1 | 0.005 | 1.15 | 0 238875 0 49 overflowCash=100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=100 toastCash=0 zeroHint=1 |
| (238875, 0, 49) | 25 | 0 | 1.35 | 0 238875 0 49 overflowCash=3400 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=3400 toastCash=0 zeroHint=1 |
| (238875, 0, 49) | 10 | 0.004 | 1.25 | 0 238875 0 49 overflowCash=1300 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=1300 toastCash=0 zeroHint=1 |
| (238875, 0, 49) | 25 | 0 | 1.05 | 0 238875 0 49 overflowCash=2600 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=2600 toastCash=0 zeroHint=1 |
| (238870, 7630, 48) | 1 | 0.005 | 1.25 | 1 238871 7631 48 overflowCash=0 toastCash=0 zeroHint=0 | 1 238871 7631 48 overflowCash=0 toastCash=0 zeroHint=0 |
| (238870, 7630, 48) | 250 | 0 | 1.05 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (238870, 7630, 48) | 25 | 0.005 | 1.35 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 | 5 238875 0 49 overflowCash=0 toastCash=0 zeroHint=0 |
| (238875, 0, 49) | 30 | 0.004 | 1.35 | 0 238875 0 49 overflowCash=4100 toastCash=0 zeroHint=1 | 0 238875 0 49 overflowCash=4100 toastCash=0 zeroHint=1 |

## V5. Respect award: 32-bit wrap-around edge cases

Output = "returned total in-level L lv=<levels crossed> hint=<+N amount>/<displayed level> cash=<overflow raw> net=<sent as network message>". All 1,968 non-edge cases (normal, capped, horde, remote, save-edited states) matched; only these differ.

| input | expected | Team B |
|---|---|---|
| table=shipped total=1 inLevel=1 L=0 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 0 lv= hint=2147483647/1 cash=0 net=0 | 238874 238875 0 49 lv=1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49 hint=238874/50 cash=0 net=0 |
| table=shipped total=119437 inLevel=397 L=31 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 31 lv= hint=2147483647/32 cash=0 net=0 | 119438 238875 0 49 lv=32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49 hint=119438/50 cash=0 net=0 |
| table=shipped total=119437 inLevel=397 L=31 amount=2147483547 horde=0 owned=1 | 2147483547 0 0 31 lv= hint=2147483547/32 cash=0 net=0 | 119438 238875 0 49 lv=32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49 hint=119438/50 cash=0 net=0 |
| table=shipped total=238800 inLevel=7560 L=48 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 48 lv= hint=2147483647/49 cash=0 net=0 | 75 238875 0 49 lv=49 hint=75/50 cash=0 net=0 |
| table=shipped total=238800 inLevel=7560 L=48 amount=2147483547 horde=0 owned=1 | 2147483547 0 0 48 lv= hint=2147483547/49 cash=0 net=0 | 75 238875 0 49 lv=49 hint=75/50 cash=0 net=0 |
| table=shipped total=238874 inLevel=7634 L=48 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 48 lv= hint=2147483647/49 cash=0 net=0 | 1 238875 0 49 lv=49 hint=1/50 cash=0 net=0 |
| table=shipped total=238874 inLevel=7634 L=48 amount=2147483547 horde=0 owned=1 | 2147483547 0 0 48 lv= hint=2147483547/49 cash=0 net=0 | 1 238875 0 49 lv=49 hint=1/50 cash=0 net=0 |
| table=100,200,300,0 total=1 inLevel=1 L=0 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 0 lv= hint=2147483647/1 cash=0 net=0 | 599 600 0 3 lv=1,2,3 hint=599/4 cash=0 net=0 |
| table=100,200,300,0 total=300 inLevel=0 L=2 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 3 lv=3 hint=2147483647/4 cash=0 net=0 | 300 600 0 3 lv=3 hint=300/4 cash=0 net=0 |
| table=100,200,300,0 total=300 inLevel=0 L=2 amount=2147483547 horde=0 owned=1 | 2147483547 0 0 3 lv=3 hint=2147483547/4 cash=0 net=0 | 300 600 0 3 lv=3 hint=300/4 cash=0 net=0 |
| table=100,200,300,0 total=525 inLevel=225 L=2 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 2 lv= hint=2147483647/3 cash=0 net=0 | 75 600 0 3 lv=3 hint=75/4 cash=0 net=0 |
| table=100,200,300,0 total=525 inLevel=225 L=2 amount=2147483547 horde=0 owned=1 | 2147483547 0 0 2 lv= hint=2147483547/3 cash=0 net=0 | 75 600 0 3 lv=3 hint=75/4 cash=0 net=0 |
| table=100,200,300,0 total=599 inLevel=299 L=2 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 2 lv= hint=2147483647/3 cash=0 net=0 | 1 600 0 3 lv=3 hint=1/4 cash=0 net=0 |
| table=100,200,300,0 total=599 inLevel=299 L=2 amount=2147483547 horde=0 owned=1 | 2147483547 0 0 2 lv= hint=2147483547/3 cash=0 net=0 | 1 600 0 3 lv=3 hint=1/4 cash=0 net=0 |
| table=1,1,5,0 total=1 inLevel=1 L=0 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 0 lv= hint=2147483647/1 cash=0 net=0 | 6 7 7 0 lv= hint=6/1 cash=0 net=0 |
| table=1,1,5,0 total=3 inLevel=3 L=0 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 0 lv= hint=2147483647/1 cash=0 net=0 | 4 7 7 0 lv= hint=4/1 cash=0 net=0 |
| table=1,1,5,0 total=6 inLevel=6 L=0 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 0 lv= hint=2147483647/1 cash=0 net=0 | 1 7 7 0 lv= hint=1/1 cash=0 net=0 |
| table=500,500,500,500,500,500,500,500,500,500 total=1 inLevel=1 L=0 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 0 lv= hint=2147483647/1 cash=0 net=0 | 4499 4500 0 9 lv=1,2,3,4,5,6,7,8,9 hint=4499/10 cash=0 net=0 |
| table=500,500,500,500,500,500,500,500,500,500 total=2250 inLevel=250 L=4 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 4 lv= hint=2147483647/5 cash=0 net=0 | 2250 4500 0 9 lv=5,6,7,8,9 hint=2250/10 cash=0 net=0 |
| table=500,500,500,500,500,500,500,500,500,500 total=2250 inLevel=250 L=4 amount=2147483547 horde=0 owned=1 | 2147483547 0 0 4 lv= hint=2147483547/5 cash=0 net=0 | 2250 4500 0 9 lv=5,6,7,8,9 hint=2250/10 cash=0 net=0 |
| table=500,500,500,500,500,500,500,500,500,500 total=4425 inLevel=425 L=8 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 8 lv= hint=2147483647/9 cash=0 net=0 | 75 4500 0 9 lv=9 hint=75/10 cash=0 net=0 |
| table=500,500,500,500,500,500,500,500,500,500 total=4425 inLevel=425 L=8 amount=2147483547 horde=0 owned=1 | 2147483547 0 0 8 lv= hint=2147483547/9 cash=0 net=0 | 75 4500 0 9 lv=9 hint=75/10 cash=0 net=0 |
| table=500,500,500,500,500,500,500,500,500,500 total=4499 inLevel=499 L=8 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 8 lv= hint=2147483647/9 cash=0 net=0 | 1 4500 0 9 lv=9 hint=1/10 cash=0 net=0 |
| table=500,500,500,500,500,500,500,500,500,500 total=4499 inLevel=499 L=8 amount=2147483547 horde=0 owned=1 | 2147483547 0 0 8 lv= hint=2147483547/9 cash=0 net=0 | 1 4500 0 9 lv=9 hint=1/10 cash=0 net=0 |
| table=50,0,50,0 total=1 inLevel=1 L=0 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 0 lv= hint=2147483647/1 cash=0 net=0 | 99 100 0 3 lv=1,2,3 hint=99/4 cash=0 net=0 |
| table=50,0,50,0 total=25 inLevel=25 L=0 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 0 lv= hint=2147483647/1 cash=0 net=0 | 75 100 0 3 lv=1,2,3 hint=75/4 cash=0 net=0 |
| table=50,0,50,0 total=50 inLevel=0 L=1 amount=2147483647 horde=0 owned=1 | 2147483647 0 2147483647 1 lv= hint=2147483647/2 cash=0 net=0 | 50 100 50 1 lv= hint=50/2 cash=0 net=0 |
| table=50,0,50,0 total=99 inLevel=49 L=1 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 1 lv= hint=2147483647/2 cash=0 net=0 | 1 100 50 1 lv= hint=1/2 cash=0 net=0 |
| table=shipped total=100 inLevel=0 L=0 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 49 lv=1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49 hint=2147483647/50 cash=0 net=0 | 238775 238875 7535 48 lv=1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48 hint=238775/49 cash=0 net=0 |
| table=shipped total=0 inLevel=2147483000 L=0 amount=1000 horde=0 owned=1 | 1000 1000 0 0 lv= hint=1000/1 cash=0 net=0 | 1000 1000 0 49 lv=1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49 hint=1000/50 cash=0 net=0 |
| table=1000,0 total=100 inLevel=0 L=0 amount=2147483647 horde=0 owned=1 | 2147483647 0 0 1 lv=1 hint=2147483647/2 cash=0 net=0 | 900 1000 900 0 lv= hint=900/1 cash=0 net=0 |
| table=1000,0 total=0 inLevel=2147483000 L=0 amount=1000 horde=0 owned=1 | 1000 1000 0 0 lv= hint=1000/1 cash=0 net=0 | 1000 1000 0 1 lv=1 hint=1000/2 cash=0 net=0 |

## V6. Rotate-left-6 / XOR name hash, byte values 128 and above

Rule (matches all 625 cases): every byte is read as a SIGNED 8-bit value (so byte value 128 becomes -128, i.e. the 32-bit value with all high 24 bits set) before the A-Z lower-casing test and the XOR. Pure-ASCII names are unaffected (all 329 ASCII cases matched). Input is the byte string in hex; outputs are unsigned decimal. An absent (null) name gives 4294967295 from the main copy of this hash and 65535 from the second copy; the two copies agree on every non-null input.

| bytes | expected | Team B |
|---|---:|---:|
| 80 | 4294967168 | 128 |
| ff | 4294967295 | 255 |
| c0e0 | 4063 | 12512 |
| 636166e9 | 4269143657 | 25823593 |
| c9434f4c45 | 3363116186 | 3364547429 |
| 6e61ef7665 | 2423972378 | 1871375845 |
| 9a8a | 6453 | 9738 |
| 616263ff646566 | 486121681 | 3774504750 |
| 739dfd8bffae3bdbcbdb4ccd61644a43 | 414810947 | 3883250623 |
| 431bc7a85a690d30d7724ad079a8ed | 1455472149 | 1430024742 |
| 0cd61f7f299bf931cf88ae6769802c70b4e30e1ba74be21b736294d5ce82c6 | 2895608886 | 2941551369 |
| b7c04c2ed36c36 | 1397039174 | 1823809654 |
| a5783671339332da3a81f22092d359c706271f | 21193636 | 4256194455 |
| 9d0e16e684a2ae70090b64bd6bde43fdf55a4c9f72ad227ba4f207cfc9a973 | 2145623829 | 3004391638 |
| 783f9afcbf4155c3c37e06effd91aa9a02433b7a69b358a84832bb97 | 4082651846 | 1012376053 |
| f60d7f3bf9cc1ef1d2174730bf8f4a0f60ae28285cce4ef4e1 | 2619656766 | 2619815229 |
| 4bfd3eda925418e704734e116c284bdbeedc60e6efe49047f13887 | 2748830069 | 2430328249 |
| a4c080c46291bf542c867890bd19c5db91c8eeb6ba23716f01e4ae8d443d6aa2bddd47ae2b48 | 164080114 | 183789298 |
| bbb5565e750c4cc0b0b1215823fdc3be45d069d2aaa5ebf82a8ef408e2a35d9f64e0a242ec27ca | 738637951 | 269125760 |
| 1f032a170826067782a5049b392780d92c0166cee40d4816b967 | 2419280487 | 2948810404 |
| 6c90e0c1b13b1b76a2 | 830468061 | 4051952402 |
| 34a0c9a63c2fb7026ef8e221a121e32d184e5e3589640ccae5175bf0ccb911 | 1123433277 | 2395589438 |
| c24120aef598 | 562369256 | 3783974376 |
| c4d196d0bf897ce0a5f9afd1dbca5ce6161e332219a303aa91b14b5448 | 3355206891 | 4109604644 |
| d287718ea3d16a1c27b03f28b394fc44 | 1607067606 | 2687837417 |
| 8e3a57ce657fad0dccce7887781d66c2c3f01f6c23e56556c250257f722fde56 | 769282451 | 317365843 |
| e93023d937ce01247573ca76c9b220d3e5e5edc84bb9bca9ab | 644276093 | 3919962289 |
| ad4db2a18c391ed575622ee4a28e5f24 | 1125894657 | 1126696702 |
| e1bd03eab8bfa7da15 | 4192566943 | 974500511 |
| 53eb44acad062be3d6b83cf9e15d963ab4689f285abb06ddd3efc1ec6f | 1473538528 | 1760060947 |
| 1b8ab767d716cc1464e8a028d8411dab06bda6 | 2269891822 | 3027778590 |
| 82c28873e921ff271c6a46cfdd01ba461a29d17d12ce061688a9 | 4073123342 | 4274655986 |
| dcc6d18ff787046d5eb8 | 2400225082 | 1892912117 |
| a0 | 4294967200 | 160 |
| 903e901ffd28 | 4329292 | 1010945868 |
| a99a3432a1757211cb3b0123931614 | 3325954536 | 3322005992 |
| d439ced3d016a223020703bee485 | 1289066828 | 2200332672 |
| dd8eb87c8cbebedb4449714f7e0f8cc5afc272 | 3774903998 | 3989615938 |
| 4ef6eba14ab46cf392f4ceb31b0a87dc94309f5ac6bf40bec42d665206 | 2411293522 | 1128621969 |
| f4ba22030e75e1b80745 | 4160354710 | 3409250710 |
| bfe875183d90f0fdc348f0196cd363d30fcd87d02bc2ddfdc35c5cc10a920be4d4a57d3a9bc86e | 1072254777 | 4242899909 |
| 35f6743795fe917f0a732c3371aee5e0c70cd8c6e38fa2bae3761277bf196dfd6c1669eee7 | 3904825648 | 3950062272 |
| b0205a64 | 4274118939 | 46276324 |
| 172f9c9d42303f1d5cffe3be5095f5e1b00ba5e25481d02692b9d2b03a3b052b1219 | 1899214835 | 1308588848 |
| cf1471c1c4ab8b691011c14537fbcfc974c84d8b65bf9e1fad3128 | 3413128494 | 75560658 |
| 7f25690b80cac1ca92a68b431a574b3bd9fbd96e6a91c5cf775fb46f1391f9e5507f63 | 3867184337 | 3854351393 |
| ce68e7214377a211a41578baee261b54922d575568bf6887de66cf89 | 622704698 | 3655174198 |
| a5417af5a601d4585b03eea71d83c5666adc97d9360cc41e9ce0 | 3766687785 | 3964610518 |
| e8 | 4294967272 | 232 |
| 2944881eae4edf3bddcd3af1f71b459912a0c9af87 | 2725153578 | 1365067817 |
| 2b1b4df483838a4bcffa5d227ad4f841353a22f6a948cf33b9257876c6d45c | 83384376 | 4214728952 |
| 4f2bdd1832528f705497806f2dd760e056d2749e3122a8caaf1e76e7f69801f644579054c4a775 | 1019958593 | 4244088398 |
| 8a517488900c61edf551b70c97597c2f051d8d8d02b9d893e71748a496e7 | 1600259036 | 1670565087 |
| ff7475bf418be24824e0312c940d25bb3f6778148cc7580a | 2739648066 | 2940504449 |
| 6af9bf3fcecaf5c4bac31099bd | 2315298672 | 1174431859 |
| 0451ac72402e31dc | 3003317095 | 1929575335 |
| bc3b766875caa559d89fb7642489685e7ed8f2f22db3088c3922531d6641aa8c0ab850b757bc | 2900430898 | 1674649602 |
| d2 | 4294967250 | 210 |
| a07c3a78a4a71b4574b551f8fe6254b16f91162c56798a03b77a | 1222441390 | 3033532065 |
| a8276cd86e3827712dece2c5fbc205bf5c6cac567ee55bfc536ddada493a90b857 | 2715743562 | 2464073142 |
| 45724d543774704555782e4745656e68725f57795a667a735558306b316b5a465738376a4d2e | 3923519676 | 3923519676 |
| 49556e4c57446d4d48756272 | 1732940554 | 1732940554 |
| 70764c | 466412 | 466412 |
| 5f385f347551385a6953335a61756d6351354f4b6750 | 3528160625 | 3528160625 |
| 374457744d30 | 2782616445 | 2782616445 |
| 6e4778 | 457144 | 457144 |
| 37725269303574 | 4087811672 | 4087811672 |
| 31426e584c47777243773248304272796464743862676279417577456c4873516f34766364 | 1753227956 | 1753227956 |
| 4e6d4834413472343141687554486e6a35777464725f6930764b4b7a41687949582e6e | 614980407 | 614980407 |
| 643975507a5f37355463796b2e456a46 | 1170713892 | 1170713892 |
| 6d36375944634177514f6c77755577453674627973564558725853787a4442424a77763965734d43 | 3774771785 | 3774771785 |
| 5469446e49624a4b626e6778304e6b554a745157504e6e67774f6c693669637275377952 | 1891544909 | 1891544909 |
| 7178594b51334a4e614942795f745139 | 285930551 | 285930551 |
| 7372335f63697479 | 2659927637 | 2659927637 |
| 492e6a48374532444432 | 3215062975 | 3215062975 |
