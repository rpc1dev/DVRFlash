                 Using DVRFlash on Linux/x86


[General Notes]

Pioneer DVR drives usually require 2 firmware files for flashing. One is
called the kernel and the other the normal part (or general part).
If you are not converting a rebadged drive to a true Pioneer, or if you are
simply applying a patched firmware, you don't necessarily need to provide a
kernel, so don't panic if you have only one firmware file.
In the following command samples, we will assume that both files are used.
Also, if you do have a kernel file, you should know that its revision does
not need to match the normal part revision. For instance, you can use a
1.05 kernel with a 1.07 normal part.

In the following samples, the kernel firmware is 'R5100004.133' and the
normal firmware is 'R5100104.133'. You will need to change those names
according to the firmwares you downloaded.
You can input these firmwares in any order you like as DVRFlash will
recognize them automatically.


[Linux Flashing]

DVRFlash is based on fPLScsi which is a derivative of PLScsi.
Since version 2.0 DVRFlash includes device autodetection and supports
native IDE devices (like /dev/hdc) provided you are using a recent kernel.
To detect your DVR drive just run DVRFlash without any parameter, and note
what device it is detected as.

Then you can use command such as:

  DVRFlash -vf /dev/hdc R5100004.133 R5100104.133

The example above will flash a DVR-105 drive (or compatible) set as
/dev/hdc.