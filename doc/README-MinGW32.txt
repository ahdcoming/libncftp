Move the source tree to a pathname that does not contain spaces,
for example, this is good:

    C:\MinGW\src\libncftp-3.2.4 

but this is bad:

    C:\Documents and Settings\Yourname\My Documents\Source\libncftp-3.2.4

There is a "mingw_makefiles.zip" file in this "doc" directory.  Extract
this zip file into the top-level directory (the parent directory of "doc"),
which will produce the Makefiles.

Edit each of these Makefiles and change TOPDIR to the top-level source
directory (this directory is the one that contains doc, libncftp, sio,
and Strn).  Note that this pathname should contain forward slashes, e.g.:

    TOPDIR=/MinGW/src/libncftp-3.2.4

From a command prompt, cd to $TOPDIR.  You should have gcc in your
PATH environment variable.  If not, edit PATH from the Control Panel's
System's, Advanced tab, then open a new command prompt window.

If PATH is set correctly, you should be able to type:

    gcc

And see that gcc prints a message about "no input files."  If you get a
command not found error, your PATH is not set correctly.  (Re-open a new shell
if you changed PATH.)

Then do:

    cd Strn
    mingw32-make.exe libStrn.a

You may see some errors, which are the result of the Makefile trying to do
UNIX specific things like "chmod," so ignore these errors.  Run make
again to verify that the library is built:

    mingw32-make.exe libStrn.a

You should see a message saying that libStrn.a is up to date.  Now build the
other libraries:

    cd ..\sio
    mingw32-make.exe libsio.a
    mingw32-make.exe libsio.a

    cd ..\libncftp
    mingw32-make.exe libncftp.a
    mingw32-make.exe libncftp.a

If the libraries are built correctly, the last thing to do is build a sample
program:

    cd samples\simpleget
    mingw32-make.exe simpleget.exe

Run the simpleget program to verify that it retrieves README.TXT from the
test server, ftp.freebsd.org.

    simpleget.exe

You are now ready to build your own programs using the LibNcFTP libraries.
Use the sample Makefiles as a template for your own.
