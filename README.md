#MDCCT
=============
Linux BURST coin plot generator optimized for SSE4 / AVX2<br>
Speed gain of ~2x using SSE4 core instead of original<br>
AVX2 not tested (report results!)<br>
<br>
Linux BURST coin miner/optimizer (untouched original code)<br>
Author: Niksa Franceschi <niksa.franceschi@gmail.com><br>
Burst for donations: BURST-RQW7-3HNW-627D-3GAEV<br>

######Original code 
Linux miner/plotter/plot optimizer by dcct / Markus Tervooren <info@bchain.info><br>
Burst: BURST-R5LP-KEL9-UYLG-GFG6T<br>

Modifed using Cerr Janror <cerr.janror@gmail.com><br>
Burst: BURST-LNVN-5M4L-S9KP-H5AAC<br>
BurstSoftware code: https://github.com/BurstTools/BurstSoftware <br>

###Installing
> git clone https://github.com/BurstTools/BurstSoftware.git
> cd mdcct
> make

Installation may break on AVX2 code (depending on compiler), but it is separate binary.<br>

###Usage
######For SSE4
> Usage: ./plot -k KEY [ -x CORE ] [-d DIRECTORY] [-s STARTNONCE] [-n NONCES] [-m STAGGERSIZE] [-t THREADS]
>   CORE:
>     0 - default core
>     1 - SSE4 core

######For AVX2
> Usage: ./plotavx2 -k KEY [ -x CORE ] [-d DIRECTORY] [-s STARTNONCE] [-n NONCES] [-m STAGGERSIZE] [-t THREADS]
>   CORE:
>     0 - default core
>     1 - SSE4 core
>     2 - AVX2 core
######Not specifying -x option will default to original dcct ploter
