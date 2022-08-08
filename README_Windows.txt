                 Using DVRFlash on Windows platforms


[General Notes]

Pioneer DVR drives usually require 2 firmware files for flashing. One is
called the kernel and the other the normal part (or general part).
If you are not converting a rebadged drive to a true Pioneer, or if you are
simply applying a patched firmware, you don't necessarily need to provide
a kernel, so don't panic if you have only one firmware file.
In the following command samples, we will assume that both files are used.
Also, if you do have a kernel file, you should know that its revision does
not need to match the normal part revision. For instance, you can use a
1.05 kernel with a 1.13 normal part.

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
2/ Give DVRFlash a try. DVRFlash will always try to help you about what you
might be doing wrong.
3/ If all of the above fails, then, AND ONLY THEN, you can try to post in
the forum with RELEVANT INFORMATION about what you are trying to do and how
you are trying to do it.

But I have to repeat; the information is already out there.
The only difference is that smart people always know how to find it...

In the following samples, the kernel firmware is 'R5100004.133' and the
normal firmware is 'R5100104.133'. You will need to change those names
according to the firmwares you downloaded.
You can input these firmwares in any order you like as DVRFlash will
recognize them automatically.

And YES you can go back and forth ANY official/patched firmware revision or
patch official/patched x.yz over official/patched x.yz.
Don't you think we would TELL YOU if it was otherwise???


[NT/2k/XP/2k3/Vista]

You don't have to install anything special. Just open a DOS Window and run
a command like:

  DVRFlash -f I: R5100004.133 R5100104.133

In this case 'I:' is the DVR drive letter.
The command above will force flash a 105 compatible drive (in I:) with the
Pioneer DVR-105 v1.33 firmware
The command above also works with USB/Firewire drives

If you don't know your drive letter, just run DVRFlash without parameters
and write down the drive letter detected by the program. Then run the
command as indicated above


[Win9x/WinMe] (and any Windows version with ASPI32 4.60 installed)

First, you need to have Adaptec ASPI32 v4.60 installed.
Then you need to figure out the SCSI ID of your drive.
Thankfully, this version of DVRFlash can do that for you. Just run DVRFlash
without parameter and write down the x.y.z SCSI ID (eg. 1.0.0)

Then enter the command:

  DVRFlash -f x.y.z R5100004.133 R5100104.133

Where x.y.z is the SCSI ID you found above

Note that you can also use the SCSI ID on an NT/2k/XP/2k3 platform if you
have ASPI32 installed.

Please do not use -vf as this can cause some issues, also when
crossflashing, you need to use the -ff command, to force flash it.
