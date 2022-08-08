                        Using DVRFlash from DOS


[General Notes]

Pioneer DVR drives usually require 2 firmware files for flashing. One is
called the kernel and the other the normal part (or general part).
If you are not converting a rebadged drive to a true Pioneer, or if you are
simply applying a patched firmware, you don't necessarily need to provide
a kernel, so don't panic if you have only one firmware file.
In the following command samples, we will assume that both files are used.
Also, if you do have a kernel file, you should know that its revision does
not need to match the normal part revision. For instance, you can use a
1.05 kernel with a 1.07 normal part.

And once and for all, don't play it more stupid than you are!
If you are worried, you probably shouldn't because people who publish the
firmwares do everything they can to provide you with exactly what you need.

In short, flashing a firmware is not the end of the world, and it is not a
license for bothering busy people with questions on how to use the flashing
tools, the files you need, or how to actually use your Operating System...

Besides, DVRFlash is pretty much bulletproof and what's more, Pioneer did
such a good job with their DVR drives that you are very unlikely to kill
one, even if you have no clue what you are doing.

If you are still unsure or worried, why don't you:
1/ Do a SEARCH at http://forum.rpc1.org
You will find that your question has probably already been ANSWERED.
2/ Give DVRFlash a try. DVRFlash will always try to help you about what
you might be doing wrong.
3/ If all of the above fails, then, AND ONLY THEN, you can try to post in
the forum with RELEVANT INFORMATION about what you are trying to do and
how you are trying to do it.

But I have to repeat; the information is already out there.
The only difference is that smart people always know how to find it...

In the following samples, the kernel firmware is 'R5100004.133' and the
normal firmware is 'R5100104.133'. You will need to change those names
according to the firmwares you downloaded.
You can input these firmwares in any order you like as DVRFlash will
recognize them automatically.


[DOS Flashing]

1/ Make sure you have all the files here plus the firmware file(s) in a
   directory available from DOS

2/ Boot your computer with a DOS bootdisk (eg. DOS/Win9x/WinMe)

3/ Go to the directory where you copied all the files and type the command:

   addDev aspi.sys       (yes, 3 D's in addDev)

   This will load the ASPI driver that is required to access your DVR drive

4/ To identify the SCSI ID of your device run DVRFlash without any parameters:

   DVRFlash

   The DVR drive(s) should be listed with their SCSI ID(s) (the x.y.z value,
   eg. 1.0.0)

5/ Now that you have found your device, you can enter the command:

   DVRFlash -f 1.0.0 R5100004.133 R5100104.133

Please do not use -vf as this can cause some issues, also when crossflashing
you need to use the -ff command, to force flash it.
