<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN"
    "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">

<meta http-equiv="Content-Type" content="text/html;charset=UTF-8">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head>
    <title>LAME MP3 Encoder :: A Good Example For Tuning Pre-Echo Detection Criterion</title>
    <meta name="author" content="Roberto Amorim - rjamorim@yahoo.com" />
    <meta name="generator" content="jEdit 5.4" />
    <meta name="cvs-version" content="$Id: preecho.php,v 1.6 2009-11-03 16:11:01 rjamorim Exp $" />
    <meta http-equiv="content-type" content="text/html; charset=UTF-8" />
    <link rel="stylesheet" type="text/css" href="styles/lame.css" />
    <!--[if IE 6]>
    <link rel="stylesheet" type="text/css" href="styles/ie.css" />
    <![endif]-->
</head>
<body>

<?php include("menu.html") ?>

<div id="container">
<div id="content">

<div align="center">
    <img src="images/logo.gif" width="358" height="231" alt="LAME Official Logo" />
    <h1>A Good Example For Tuning Pre-Echo Detection Criterion</h1>
</div>

<p>
    Make sure you have LAME 3.04 or greater. there were some bugs in the syncing
    of mp3x (the frame analyzer.)
</p>

<p>
    Find a sample with a noticeable artifact. testsignal2.wv is a good example
    (see <a href="quality.php">tests.php</a>) LAME will produce obvious and
    artificial "snare" like sounds that should not be there. FhG sounds pretty
    good. If the FhG encoder has the same problem, then good luck getting GPSYCHO
    to sound better than FhG!
</p>

<p>
    Figure out what frame causes the problem. I use mpg123 for this. For example,
    to play just 50 frames: <tt>"mpg123 -n 50 testsignal2.mp3"</tt>. By trying
    different values for -n, you can usually get a pretty good estimate of which
    frames are bad.
</p>

<p>
    For testsignal2.wav, the first problem is around frame 25.
</p>

<p>
    Look at the frames with mp3x ('lame -g', the graphical frame analyzer). To
    get the frames from different encoders perfectly in sync with each other, you
    need to compensate for the various delays introduced by the encoder.
</p>

<p>
    Encoders have different amounts of built in delay, meaning the mp3 file will
    add extra padding to the pcm data when it is decoded. All known decoders
    (including mpglib/mpg123 used by mp3x) also introduce their own delay,
    usually an additional 528 samples. So for the total amount of padding between
    the input file and a decoded output file, add 528 to the numbers below.
</p>

<p>
    Encoder delays:
</p>

<pre>ISO models &amp; MPECKER:     528 samples
LAME 3.11 and earlier:    528 samples
LAME 3.12-3.50           1160 samples
LAME 3.54 and newer        48 samples, adjustable - see ENCDELAY in encoder.h
FhG mp3enc31:            1160 samples</pre>

<p>
    To use mp3x to do a direct frame by frame comparison between the various
    encoders, you need to add padding to the input file so that the encoded frames
    will be perfectly in sync. To add 632 bytes of padding to your input file,
    create a file "pad632.pcm" which just contains 4*632 bytes, all zero. Then:
</p>

<blockquote class="code">sox file.wav -t raw -x -w -s -c 2 -r 44100 temp.pcm
cat pad632.pcm temp.pcm &gt; temp2.pcm
sox -t raw -x -w -s -c 2 -r 44100 temp2.pcm file632.wav
l3enc -if file632.wav -of file632.mp3
lame -g file632.mp3</blockquote>

<p>
    (note: the following frame numbers refered to LAME 3.11, and FhG applied to
    the .wav file with 520 bytes of padding)
</p>

<p>
    For testsignal2.wav, you can see frame 25 (granule 1) and frame 26 (granule
    0), LAME does not use short blocks and FhG does. For these frames, the
    pe=1200. Forcing LAME to switch to short blocks for these (and other similar
    frames) by setting switch_pe=1000 will produce a MP3 file almost as good as
    FhG's.
</p>

<p>
    So why not just change pe_switch to 1000? It might be a good idea, but then
    LAME will use about 2x as many short blocks as FhG. For example, in <a
    href="quality.php">else3.wv</a>, frame 29 (granule=1), the pe=1500, but FhG
    does not switch to short blocks. To compute the total number of short blocks
    in a MP3, run mp3x out to the last frame and then click on the "Stats" pull
    down menu. It will show a bunch of statistics from the MP3 file.
</p>

<p>
    Note: the technique described above was used to tune some new window
    switching algorithms in LAME 3.10. These new tunings solve all the
    problems mentioned above!
</p>

</div>
<?php include("footer.html") ?>
</div>

</body>
</html>