Since you have the source distribution, you will need to compile the
libraries before you can install them.

UNIX INSTRUCTIONS:
------------------

Go to the ../libncftp directory. There is a script you must run which
will checks your system for certain features, so that the library can
be compiled on a variety of UNIX systems.  Run this script by typing
"./configure" in that directory.  After that, you can look at the
Makefile it made if you like, and then you run "make" to create the
"libncftp.a", "libStrn.a", and "libsio.a" library files.

Finally, install the libraries and headers, by doing "make install".

View the libncftp.html file for the rest of the documentation.  An easy
way to do that is use a URL of file://Localhost/path/to/libncftp.html
with your favorite browser.


WINDOWS INSTRUCTIONS:
---------------------

You will need Visual C++ 2005 or greater to build the library and sample
programs.  This version includes two supplementary libraries which you
must build and link with your applications: a string utility library
(Strn) and a Winsock utility library (sio).

Keep the source hierarchy intact, so that the samples and libraries
build without problems.  Each project directory contains a visual studio
project (.vcproc and .sln files), but you don't need to manually build
each project.  Instead, open the "LibNcFTP_ALL.sln" workspace file
in the libncftp directory, and then "Build Solution" to build all the
libraries and all the samples.

View the libncftp.html file for the rest of the documentation.  An easy
way to do that is use a URL of file://Localhost/path/to/libncftp.html
with your favorite browser.  Note that there may be UNIX-specific
instructions which you should ignore.

To build your own applications using LibNcFTP, you'll need to make sure
you configure your project to find the header files and library files.

Your application may not need to use the sio or Strn libraries directly,
but you still need to link with them.  For example, the "simpleget" sample
uses "..\..\$(ConfigurationName),..\..\..\Strn\$(ConfigurationName),..\..\..\sio\$(ConfigurationName)"
in the project option for additional library paths for the linker, and
"ws2_32.lib libncftp.lib Strn.lib sio.lib" for the list of libraries
to link with, where the system Winsock (ws2_32.lib) is also linked in.

Similarly, you'll need to make sure one of your additional include
directories points to the LibNcFTP directory containing ncftp.h.  The
"simpleget" sample uses "..\.." since it is in a subdirectory of the
library itself.  If you actually use functions from Strn or sio (as
some of the samples do), you'll need to have your project look in
their directories for their headers as well.


WINDOWS INSTRUCTIONS FOR CREATING A LIBNCFTP PROJECT FROM SCRATCH:
------------------------------------------------------------------

Let's assume you have installed the library and header files, with sio.lib,
Strn.lib, and libncftp.lib in the directory C:\local\lib, and the sio.h,
Strn.h, ncftp.h, and ncftp_errno.h in the directory C:\local\include.

Create a new project from within the Visual Studio IDE.  In this example,
we're just re-creating the simpleget sample program as a separate project,
so we choose Win32 Console Application as the project type.  Step through
the wizard and choose "Console application" as the Application type and
click the "Empty project" checkbox, then finish the wizard.

Copy the file libncftp\samples\simpleget\simpleget.c to the newly created
project's source directory.  Right click on the project's Source folder
in the Solution Explorer tab and do "Add > Existing item..." then browse
for the simpleget.c file you just copied.

Right click on the project and choose "Properties."  Under the "Configuration:"
menu, choose "All Configurations" so that the changes we make here will
apply to both the Debug and Release configurations.  Under the "Configuration
Properties" section, view the first page of the "C++" section ("General").
Enter "C:\local\include" in the "Additional Include Directories."  

Next, view the first page of the "Linker" section ("General"), and enter
"C:\local\lib" in the "Additional Library Directories" field.  Then proceed
to the "Input" page of the "Linker" section and in the "Additional
Dependencies" field, enter "ws2_32.lib libncftp.lib Strn.lib sio.lib".

Finally, "Build Solution" and the result should be a working application.
