                 Using DVRFlash on Mac OS X platforms
           Read this entire document BEFORE posting questions!


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

And YES you can go back and forth ANY official/patched firmware revision
or patch official/patched x.yz over official/patched x.yz
Don't you think we would TELL YOU if it was otherwise???


[Mac OS X Flashing]

You don't have to install anything special. Just open a Terminal (Located
in your Utilities folder) window and set the directory to your DVRFlash
folder. Be sure that your firmware file(s) are in the same folder. The
simplest way to do this is to place DVRFlash and your firmware file(s) into
a folder called "DVRFlash" on your Desktop. Now enter the following into a
Terminal window:

  cd Desktop/DVRFlash

Now run a command like:

  ./DVRFlash -f PIONEER R5100004.133 R5100104.133

Please do not use -vf as this can cause some issues, also when cross
flashing, you need to use the -ff command, to force flash it

If you have more than one Pioneer Drive, run DVRFlash without any
parameters to discover the driver identifier (Like A:, B:, etc.) and add
that just before the word PIONEER (IE: A:PIONEER).
Alternatively, if your drives are of different models, you may use the full
name if the drive enclosed in  quotes. IE (Note the two spaces before the
drive model):

  ./DVRFlash -vf "PIONEER DVD/RW  DVR-105" R5100004.133 R5100104.133

The command above will force flash a 105 compatible drive with the Pioneer
DVR-105 v1.33 firmware The command above also works with USB and Firewire
drives.

Note: The word 'PIONEER' refers to the Vendor Name of the drive. This may
appear differently on some OEM drives. Verify the name by entering
'./DVRFlash' without any parameters.

If flashing a drive to an official update that includes only a single
firmware file, the 'f' (force) parameter is not necessary. Neither is the
'v' (verbose) parameter ever necessary. For example:

   ./DVRFlash PIONEER R5100104.133

will update a Pioneer DVR-105 to version 1.33 without the kernel file.
It is most important that both firmware and kernel files be used when
downgrading a drive or cross-flashing OEM models


DVRFlash HowTo (for Mac users with little command line practice)

To keep things as simple as possible, DVRFlash and the firmware file(s)
should be side by side in the same folder, whatever it's name or position.
So, put DVRFlash and the firmware file{s) in one folder and have that
folder's Finder window open.

Start "Terminal". (Double click Terminal's icon, do NOT double click
DVRFlash!) From the Finder window, drag DVRFlash into the terminal window.
The complete path to DVRFlash will appear, followed by a space.

Now type "-f PIONEER " without the quotes; mind the spaces before and
behind PIONEER.

Drag the firmware file into the terminal window. If you got two files
(firmware and kernel), drag them both, one after the other. The path(s) to
the firmware file(s) will appear in the term window. (That way you created
a command line with complete pathes, that works, no matter where all your
files are.)

NOW press the Return key to start it all and it should work.
