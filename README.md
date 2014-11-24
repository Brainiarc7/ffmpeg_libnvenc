FFmpeg README
=============

FFmpeg is a collection of libraries and tools to process multimedia content
such as audio, video, subtitles and related metadata.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides a mean to alter decoded Audio and Video through chain of filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](http://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](http://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](http://ffmpeg.org/ffprobe.html) is a simple analisys tool to inspect
  multimedia content.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

* The offline documentation is available in the **doc/** directory.
* The online documentation is available in [ffmpeg website](http://ffmpeg.org) and in the [ffmeg wiki](http://trac.ffmpeg.org).

## Examples

* Coding examples are available in the **doc/example** directory.


### Notes on Building on Linux

* Download and install the [NVIDIA NVENC SDK for Windows](https://developer.nvidia.com/nvidia-video-codec-sdk) (Yes, that is correct). Extract the SDK's content then navigate to `nvenc-xx.0/samples/nvEncoder/inc` and copy the header files therein to `/usr/include`.
* Ensure that the NVIDIA CUDA SDK is installed, and copy `cuda.h` to `/usr/include`.
* You can then run `./configure` with your customization options as is deemed necessary.
* In my case, I built it with NVENC enabled with the following configuration options on Ubuntu 14.04 LTS:

	```
	./configure --enable-nonfree --enable-gpl --enable-version3 --enable-libass \
	--enable-libbluray --enable-libmp3lame --enable-libopencv --enable-libopenjpeg \
	--enable-libopus --enable-libfaac --enable-libfdk-aac --enable-libtheora \
	--enable-libvpx --enable-libwebp --enable-opencl --enable-x11grab --enable-opengl \
	--enable-openssl --cpu=native --enable-libnvenc
	```

#### Result:

* The build was successful, and here's sample output:
	```
	lin@mordor:~$ ffmpeg
	ffmpeg version git-2014-10-24-3b5a7bd Copyright (c) 2000-2014 the FFmpeg developers
	built on Nov 21 2014 09:57:10 with gcc 4.8 (Ubuntu 4.8.2-19ubuntu1)
	configuration: --enable-nonfree --enable-gpl --enable-version3 --enable-libass --enable-libbluray --enable-libmp3lame --enable-libopencv --enable-libopenjpeg --enable-libopus --enable-libfaac --enable-libfdk-aac --enable-libtheora --enable-libvpx --enable-libwebp --enable-opencl --enable-x11grab --enable-opengl --enable-openssl --cpu=native --enable-libnvenc
	libavutil 54. 10.100 / 54. 10.100
	libavcodec 56. 8.102 / 56. 8.102
	libavformat 56. 9.101 / 56. 9.101
	libavdevice 56. 1.100 / 56. 1.100
	libavfilter 5. 1.106 / 5. 1.106
	libswscale 3. 1.101 / 3. 1.101
	libswresample 1. 1.100 / 1. 1.100
	libpostproc 53. 3.100 / 53. 3.100
	Hyper fast Audio and Video encoder
	usage: ffmpeg [options] [[infile options] -i infile]... {[outfile options] outfile}...

	Use -h to get full help or, even better, run 'man ffmpeg'
	```

* Here's a log of the awesome FFmpeg build with libnvenc selected as the encoder. The source file used is a 4k video, transcoded to H.264 AVC via libnvenc at a lower bitrate. 

> Hint:
> It was a great success!

```
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from '/media/lin/AUXILLIARY/Extended Media Libraries/More Video/Sample 3D/sintel_4k.mp4':
Metadata:
major_brand : avc1
minor_version : 0
compatible_brands: isomavc1
creation_time : 2014-08-26 07:57:06
encoder : Hybrid 2013.12.06.2
Duration: 00:14:48.02, start: 0.000000, bitrate: 49126 kb/s
Stream #0:0(eng): Video: h264 (High) (avc1 / 0x31637661), yuv420p(tv, bt709/unknown/unknown), 3840x1744 [SAR 1:1 DAR 240:109], 48956 kb/s, 24 fps, 24 tbr, 24k tbn, 48 tbc (default)
Metadata:
creation_time : 2014-08-26 07:57:06
Stream #0:1(eng): Audio: aac (mp4a / 0x6134706D), 44100 Hz, stereo, fltp, 167 kb/s (default)
Metadata:
creation_time : 2014-08-26 01:42:44
handler_name : Sound Media Handler
Output #0, matroska, to '/home/lin/Desktop/Encodes/traGtor.output.mkv':
Metadata:
major_brand : avc1
minor_version : 0
compatible_brands: isomavc1
encoder : Lavf56.9.101
Stream #0:0(eng): Video: h264 (libnvenc) (H264 / 0x34363248), yuv420p, 3840x1744 [SAR 1:1 DAR 240:109], q=-1--1, 25000 kb/s, 24 fps, 1k tbn, 24 tbc (default)
Metadata:
creation_time : 2014-08-26 07:57:06
encoder : Lavc56.8.102 libnvenc
Stream #0:1(eng): Audio: ac3 ([0] [0][0] / 0x2000), 44100 Hz, stereo, fltp, 160 kb/s (default)
Metadata:
creation_time : 2014-08-26 01:42:44
handler_name : Sound Media Handler
encoder : Lavc56.8.102 ac3
Stream mapping:
Stream #0:0 -> #0:0 (h264 (native) -> h264 (libnvenc))
Stream #0:1 -> #0:1 (aac (native) -> ac3 (native))
Press [q] to stop, [?] for help
video:2505473kB audio:17344kB subtitle:0kB other streams:0kB global headers:0kB muxing overhead: 0.016179%
```

## Caveats

* Extra notes: NVIDIA added NVENC support to GeForce GPUs on Linux in the R346 Beta driver.
* Earlier drivers will NOT work.
* Also, NVENC works only with NVIDIA Kepler, Maxwell and (possibly) future iterations of NVIDIA GPU architectures.
* Unlike NVCUVENC, NVENC is a dedicated SIP block for accelerated video processing, and as such, is independent of CUDA cores.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under GPL. Please refer to the [LICENSE](LICENSE.md) file for detailed information.
