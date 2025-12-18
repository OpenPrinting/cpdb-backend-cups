#!/bin/sh
#
# Perform testing of printing via CPDB and the CPDB CUPS backend
#
# Copyright © 2020-2023 by OpenPrinting
# Copyright © 2007-2021 by Apple Inc.
# Copyright © 1997-2007 by Easy Software Products, all rights reserved.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

#
# We use "dbus-run-session" to have our own, private D-Bus session bus,
# to avoid any interaction with the host system.
#

if ! test "${1}" = "d"; then
    exec dbus-run-session -- ${0} d
fi

#
# Clean up after the tests, and preserve logs for failed "make check"
# Shut down daemons, remove test bed if we have created one
#

clean_up()
{
    #
    # Shut down all the daemons
    #

    kill_sent=0

    if (test "x$BACKEND_PID" != "x"); then
	kill -TERM $BACKEND_PID 2>/dev/null
	kill_sent=1
    fi

    if (test "x$FRONTEND_PID" != "x"); then
	kill -TERM $FRONTEND_PID 2>/dev/null
	kill_sent=1
    fi

    if (test "x$cupsd" != "x"); then
	kill -TERM $cupsd 2>/dev/null
	kill_sent=1
    fi

    if test $kill_sent = 1; then
	sleep 5
    fi

    #
    # Hard-kill any remaining daemon
    #

    if (test "x$BACKEND_PID" != "x"); then
	kill -KILL $BACKEND_PID 2>/dev/null
    fi

    if (test "x$FRONTEND_PID" != "x"); then
	kill -KILL $FRONTEND_PID 2>/dev/null
    fi

    if (test "x$cupsd" != "x"); then
	kill -KILL $cupsd 2>/dev/null
    fi

    #
    # Preserve logs in case of failure
    #

    if test "x$RES" != "x0"; then
	if test -n "$BASE"; then
	    echo "============================"
	    echo "CPDB FRONTEND LOG"
	    echo "============================"
	    echo ""
	    cat $BASE/log/frontend_log
	    echo ""
	    echo ""
	    echo "============================"
	    echo "CPDB BACKEND LOG"
	    echo "============================"
	    echo ""
	    cat $BASE/log/backend_log
	    echo ""
	    echo ""
	    echo "============================"
	    echo "CUPS ERROR_LOG"
	    echo "============================"
	    echo ""
	    cat $BASE/log/error_log
	    echo ""
	    echo ""
	    echo "============================"
	    echo "CUPS ACCESS_LOG"
	    echo "============================"
	    echo ""
	    cat $BASE/log/access_log
	    echo ""
	    echo ""
	    echo "============================"
	    echo "CUPSD DEBUG LOG"
	    echo "============================"
	    echo ""
	    cat $BASE/log/cupsd_debug_log
	    echo ""
	    echo ""
	fi
    fi

    #
    # Remove test bed directories
    #

    #return
    if test -n "$BASE"; then
	rm -rf $BASE
    fi
}


#
# Call clean_up() whenever this script gets interrupted, both by signals
# end by errors...
#

RES=1
trap clean_up 0 EXIT INT QUIT ABRT PIPE TERM

#
# Force the permissions of the files we create...
#

umask 022

#
# Solaris has a non-POSIX grep in /bin...
#

if test -x /usr/xpg4/bin/grep; then
    GREP=/usr/xpg4/bin/grep
else
    GREP=grep
fi

#
# Figure out the proper echo options...
#

if (echo "testing\c"; echo 1,2,3) | $GREP c >/dev/null; then
    ac_n=-n
    ac_c=
else
    ac_n=
    ac_c='\c'
fi

#
# CUPS resource directories of the system
#
# For "make check" testing we copy/link our testbed CUPS
# components from there
#

sys_datadir=`cups-config --datadir`
sys_serverbin=`cups-config --serverbin`
sys_serverroot=`cups-config --serverroot`

#
# Information for the server/tests...
#

echo "Running 'make check' ..."
echo ""
echo "Running own CUPS instance on alternative port"
echo "Using CPDB CUPS backend from source tree"
port="${CUPS_TESTPORT:=8631}"
CUPS_TESTROOT=.; export CUPS_TESTROOT

BASE="${CUPS_TESTBASE:=}"
if test -z "$BASE"; then
    if test -d /private/tmp; then
	BASE=/private/tmp/cpdb-backend-cups-$user
    else
	BASE=/tmp/cpdb-backend-cups-$USER
    fi
fi
export BASE

#
# Make sure that the LPDEST and PRINTER environment variables are
# not included in the environment that is passed to the tests.  These
# will usually cause tests to fail erroneously...
#

unset LPDEST
unset PRINTER

#
# Start by creating temporary directories for the tests...
#

echo "Creating directories for test..."

rm -rf $BASE
mkdir $BASE
mkdir $BASE/bin
mkdir $BASE/bin/backend
mkdir $BASE/bin/driver
mkdir $BASE/bin/filter
mkdir $BASE/cache
mkdir $BASE/certs
mkdir $BASE/share
mkdir $BASE/share/banners
mkdir $BASE/share/drv
mkdir $BASE/share/locale
for file in $sys_datadir/locale/*/cups_*.po; do
    loc=`basename $file .po | cut -c 6-`
    mkdir $BASE/share/locale/$loc
    ln -s $file $BASE/share/locale/$loc
done
mkdir $BASE/share/data
mkdir $BASE/share/mime
mkdir $BASE/share/model
mkdir $BASE/share/ppdc
mkdir $BASE/interfaces
mkdir $BASE/log
mkdir $BASE/ppd
mkdir $BASE/spool
mkdir $BASE/spool/temp
mkdir $BASE/ssl

#
# We copy the cupsd executable to break it off from the Debian/Ubuntu
# package's AppArmor capsule, so that it can work with our test bed
# directories
#

cp /usr/sbin/cupsd $BASE/bin/

ln -s $sys_serverbin/backend/dnssd $BASE/bin/backend
ln -s $sys_serverbin/backend/http $BASE/bin/backend
ln -s $sys_serverbin/backend/ipp $BASE/bin/backend
ln -s ipp $BASE/bin/backend/ipps
ln -s $sys_serverbin/backend/lpd $BASE/bin/backend
ln -s $sys_serverbin/backend/mdns $BASE/bin/backend
ln -s $sys_serverbin/backend/snmp $BASE/bin/backend
ln -s $sys_serverbin/backend/socket $BASE/bin/backend
ln -s $sys_serverbin/backend/usb $BASE/bin/backend
ln -s $sys_serverbin/cgi-bin $BASE/bin
ln -s $sys_serverbin/monitor $BASE/bin
ln -s $sys_serverbin/notifier $BASE/bin
ln -s $sys_serverbin/daemon $BASE/bin
ln -s $sys_serverbin/filter/commandtops $BASE/bin/filter
ln -s $sys_serverbin/filter/gziptoany $BASE/bin/filter
ln -s $sys_serverbin/filter/pstops $BASE/bin/filter
ln -s $sys_serverbin/filter/rastertoepson $BASE/bin/filter
ln -s $sys_serverbin/filter/rastertohp $BASE/bin/filter
ln -s $sys_serverbin/filter/rastertolabel $BASE/bin/filter
ln -s $sys_serverbin/filter/rastertopwg $BASE/bin/filter
cat >$BASE/share/banners/standard <<EOF
           ==== Cover Page ====


      Job: {?printer-name}-{?job-id}
    Owner: {?job-originating-user-name}
     Name: {?job-name}
    Pages: {?job-impressions}


           ==== Cover Page ====
EOF
cat >$BASE/share/banners/classified <<EOF
           ==== Classified - Do Not Disclose ====


      Job: {?printer-name}-{?job-id}
    Owner: {?job-originating-user-name}
     Name: {?job-name}
    Pages: {?job-impressions}


           ==== Classified - Do Not Disclose ====
EOF
ln -s $sys_datadir/drv/sample.drv $BASE/share/drv
ln -s $sys_datadir/mime/mime.types $BASE/share/mime
ln -s $sys_datadir/mime/mime.convs $BASE/share/mime
ln -s $sys_datadir/ppdc/*.h $BASE/share/ppdc
ln -s $sys_datadir/ppdc/*.defs $BASE/share/ppdc
ln -s $sys_datadir/templates $BASE/share
ln -s $sys_datadir/ipptool $BASE/share

#
# pdftopdf filter of cups-filters 1.x or 2.x, cgpdftopdf of Mac/Darwin,
# or gziptoany as dummy filter if nothing better installed
#
	
cp test.convs $BASE/share/mime

if test -x "$sys_serverbin/filter/pdftopdf"; then
    ln -s "$sys_serverbin/filter/pdftopdf" "$BASE/bin/filter/pdftopdf"
elif test -x "$sys_serverbin/filter/cgpdftopdf"; then
    ln -s "$sys_serverbin/filter/cgpdftopdf" "$BASE/bin/filter/pdftopdf"
else
    ln -s "gziptoany" "$BASE/bin/filter/pdftopdf"
fi

#
# Then create the necessary config files...
#

echo "Creating cupsd.conf for test..."

jobhistory="30"
jobfiles="Off"

cat >$BASE/cupsd.conf <<EOF
StrictConformance Yes
Browsing Off
Listen localhost:$port
Listen $BASE/sock
MaxSubscriptions 3
MaxLogSize 0
AccessLogLevel actions
LogLevel debug
LogTimeFormat usecs
PreserveJobHistory $jobhistory
PreserveJobFiles $jobfiles
<Policy default>
<Limit All>
Order Allow,Deny
</Limit>
</Policy>
WebInterface yes
EOF

cat >$BASE/cups-files.conf <<EOF
FileDevice yes
User $USER
ServerRoot $BASE
StateDir $BASE
ServerBin $BASE/bin
CacheDir $BASE/cache
DataDir $BASE/share
DocumentRoot .
RequestRoot $BASE/spool
TempDir $BASE/spool/temp
AccessLog $BASE/log/access_log
ErrorLog $BASE/log/error_log
PageLog $BASE/log/page_log

PassEnv DYLD_INSERT_LIBRARIES
PassEnv DYLD_LIBRARY_PATH
PassEnv LD_LIBRARY_PATH
PassEnv LD_PRELOAD
PassEnv LOCALEDIR
PassEnv ASAN_OPTIONS

Sandboxing Off
EOF

#
# Set up some test queues with PPD files...
#

echo "Creating printers.conf for test..."

i=1
while test $i -le 2; do
    cat >>$BASE/printers.conf <<EOF
<Printer printer-$i>
Accepting Yes
DeviceURI file:/dev/null
Info Test PS printer $i
JobSheets none none
Location CUPS test suite
State Idle
StateMessage Printer $1 is idle.
</Printer>
EOF

    cp testpdf.ppd $BASE/ppd/printer-$i.ppd

    i=`expr $i + 1`
done

if test -f $BASE/printers.conf; then
    cp $BASE/printers.conf $BASE/printers.conf.orig
else
    touch $BASE/printers.conf.orig
fi

#
# Create a helper script to run programs with...
#

echo "Setting up environment variables for test..."

if test "x$ASAN_OPTIONS" = x; then
    # AddressSanitizer on Linux reports memory leaks from the main function
    # which is basically useless - in general, programs do not need to free
    # every object before exit since the OS will recover the process's
    # memory.
    ASAN_OPTIONS="detect_leaks=false"
    export ASAN_OPTIONS
fi

# These get exported because they don't have side-effects...
CUPS_DISABLE_APPLE_DEFAULT=yes; export CUPS_DISABLE_APPLE_DEFAULT
CUPS_SERVER=localhost:$port; export CUPS_SERVER
CUPS_SERVERROOT=$BASE; export CUPS_SERVERROOT
CUPS_STATEDIR=$BASE; export CUPS_STATEDIR
CUPS_DATADIR=$BASE/share; export CUPS_DATADIR
IPP_PORT=$port; export IPP_PORT
LOCALEDIR=$BASE/share/locale; export LOCALEDIR

echo "Creating wrapper script..."

runcups="$BASE/runcups"; export runcups

echo "#!/bin/sh" >$runcups
echo "# Helper script for running CUPS test instance." >>$runcups
echo "" >>$runcups
echo "# Set required environment variables..." >>$runcups
echo "CUPS_DATADIR=\"$CUPS_DATADIR\"; export CUPS_DATADIR" >>$runcups
echo "CUPS_SERVER=\"$CUPS_SERVER\"; export CUPS_SERVER" >>$runcups
echo "CUPS_SERVERROOT=\"$CUPS_SERVERROOT\"; export CUPS_SERVERROOT" >>$runcups
echo "CUPS_STATEDIR=\"$CUPS_STATEDIR\"; export CUPS_STATEDIR" >>$runcups
echo "DYLD_INSERT_LIBRARIES=\"$DYLD_INSERT_LIBRARIES\"; export DYLD_INSERT_LIBRARIES" >>$runcups
echo "DYLD_LIBRARY_PATH=\"$DYLD_LIBRARY_PATH\"; export DYLD_LIBRARY_PATH" >>$runcups
# IPP_PORT=$port; export IPP_PORT
echo "LD_LIBRARY_PATH=\"$LD_LIBRARY_PATH\"; export LD_LIBRARY_PATH" >>$runcups
echo "LD_PRELOAD=\"$LD_PRELOAD\"; export LD_PRELOAD" >>$runcups
echo "LOCALEDIR=\"$LOCALEDIR\"; export LOCALEDIR" >>$runcups
if test "x$CUPS_DEBUG_LEVEL" != x; then
    echo "CUPS_DEBUG_FILTER='$CUPS_DEBUG_FILTER'; export CUPS_DEBUG_FILTER" >>$runcups
    echo "CUPS_DEBUG_LEVEL=$CUPS_DEBUG_LEVEL; export CUPS_DEBUG_LEVEL" >>$runcups
    echo "CUPS_DEBUG_LOG='$CUPS_DEBUG_LOG'; export CUPS_DEBUG_LOG" >>$runcups
fi
echo "" >>$runcups
echo "# Run command..." >>$runcups
echo "exec \"\$@\"" >>$runcups

chmod +x $runcups

#
# Set a new home directory to avoid getting user options mixed in and for CPDB
# not complain about missing directory for saving configuration...
#

HOME=$BASE
export HOME
XDG_CONFIG_HOME=${BASE}/.config
export XDG_CONFIG_HOME
mkdir ${BASE}/.config

#
# Force POSIX locale for tests...
#

LANG=C
export LANG

LC_MESSAGES=C
export LC_MESSAGES

#
# Check syntax of CUPS config files...
#

echo "Testing CUPS config files:"
echo "    $runcups $BASE/bin/cupsd -t -s $BASE/cups-files.conf"
echo ""

$runcups $BASE/bin/cupsd -t -s $BASE/cups-files.conf

echo "    $runcups $BASE/bin/cupsd -t -c $BASE/cupsd.conf"
echo ""

$runcups $BASE/bin/cupsd -t -c $BASE/cupsd.conf

echo ""

#
# Start the CUPS server; run as foreground daemon in the background...
#

echo "Starting cupsd:"
echo "    $runcups $BASE/bin/cupsd -c $BASE/cupsd.conf -f >$BASE/log/cupsd_debug_log 2>&1 &"
echo ""

$runcups $BASE/bin/cupsd -c $BASE/cupsd.conf -f >$BASE/log/cupsd_debug_log 2>&1 &

cupsd=$!

if (test "x$cupsd" != "x"); then
    echo "cupsd is PID $cupsd."
    echo ""
fi

sleep 2

#
# Fire up the CPDB CUPS backend
#

BACKEND=./cups

# Directory for job transfer sockets
mkdir $BASE/cpdb
mkdir $BASE/cpdb/sockets

# Create the log file
BACKEND_LOG=$BASE/log/backend_log
rm -f $BACKEND_LOG
touch $BACKEND_LOG

# Debug logging for CPDB
export CPDB_DEBUG_LEVEL=debug

echo "Starting CPDB CUPS backend:"
echo "    $runcups $BACKEND >$BACKEND_LOG 2>&1 &"
echo ""

$runcups $BACKEND >$BACKEND_LOG 2>&1 &
BACKEND_PID=$!

if (test "x$BACKEND_PID" != "x"); then
    echo "CPDB CUPS backend is PID $BACKEND_PID."
    echo ""
fi

#
# Run the test frontend and feed in commands.
#

FRONTEND=cpdb-text-frontend
QUEUE=printer-1
FILE_TO_PRINT=/usr/share/cups/data/default-testpage.pdf

# Set default page size to Envelope #10
$runcups lpadmin -p $QUEUE -o PageSize=Env10

# Disable queue to hold output file in spool directory
$runcups cupsdisable $QUEUE

# Create the log file
FRONTEND_LOG=$BASE/log/frontend_log
rm -f $FRONTEND_LOG
touch $FRONTEND_LOG

echo "Starting CPDB CUPS frontend:"
echo "    $FRONTEND > $FRONTEND_LOG 2>&1 &"
echo ""

( \
  sleep 5; \
  echo get-all-options $QUEUE CUPS; \
  sleep 2; \
  echo print-file $FILE_TO_PRINT $QUEUE CUPS; \
  sleep 3; \
  echo stop \
) | $FRONTEND > $FRONTEND_LOG 2>&1 &
FRONTEND_PID=$!

if (test "x$FRONTEND_PID" != "x"); then
    echo "CPDB frontend is PID $FRONTEND_PID."
    echo ""
fi

#
# Give the frontend a maximum of 15 seconds to run and then kill it, to avoid
# the script getting stuck if stopping the frontend fails.
#

i=0
while kill -0 $FRONTEND_PID >/dev/null 2>&1; do
    i=$((i+1))
    if test $i -ge 15; then
	kill -KILL $FRONTEND_PID >/dev/null 2>&1 || true
	FRONTEND_PID=
	echo "FAIL: Frontend keeps running!"
	exit 1
    fi
    sleep 1
done
FRONTEND_PID=

#
# Stop the backend
#

if kill -0 $BACKEND_PID >/dev/null 2>&1; then
    kill -TERM $BACKEND_PID
    # Give the backend a maximum of 3 seconds to shut down and then kill it,
    # to avoid the script getting stuck if shutdown fails.
    i=0
    while kill -0 $BACKEND_PID >/dev/null 2>&1; do
	i=$((i+1))
	if test $i -ge 3; then
	    kill -KILL $BACKEND_PID >/dev/null 2>&1 || true
	    BACKEND_PID=
	    echo "FAIL: Backend did not shut down!"
	    exit 1
	fi
	sleep 1
    done
fi
BACKEND_PID=

#
# Check log for test results
#

# Does the printer appear in the initial list of available printers?
echo "Initial listing of the printer:"
if ! grep '^Printer '$QUEUE'$' $FRONTEND_LOG; then
    echo "FAIL: CUPS queue $QUEUE not listed!"
    exit 1
fi

echo

# Does the attribute "printer-resolution" appear in the list of options?
echo "Attribute listing of \"printer-resolution\":"
if ! grep 'printer-resolution' $FRONTEND_LOG; then
    echo "FAIL: Attribute \"printer-resolution\" not listed!"
    exit 1
fi

echo

# Does the attribute "print-color-mode" appear in the list of options?
echo "Attribute listing of \"print-color-mode\":"
if ! grep 'print-color-mode' $FRONTEND_LOG; then
    echo "FAIL: Attribute \"print-color-mode\" not listed!"
    exit 1
fi

echo

# Does the setting "na_number-10_4.125x9.5in" appear as a default setting?
echo "\"na_number-10_4.125x9.5in\" as a default setting:"
if ! grep 'DEFAULT: *na_number-10_4.125x9.5in' $FRONTEND_LOG; then
    echo "FAIL: Setting \"na_number-10_4.125x9.5in\" not listed as default!"
    exit 1
fi

echo

# Did the successful submission of a print job get confirmed?
echo "Confirmation message for job submission:"
if ! grep -i 'Document send succeeded' $BACKEND_LOG; then
    echo "FAIL: No confirmation of job submission!"
    exit 1
fi

echo

#
# Tests successfully completed
#

echo "SUCCESS: All tests were successful."
echo ""

RES=0
exit $RES
