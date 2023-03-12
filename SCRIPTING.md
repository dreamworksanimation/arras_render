New command line options
—script {path to the script file}
—run-script
Indicate that the script should be run automatically rather than waiting for a button press
—exit-after-script
Indicate that arras4_rdla2exr should exit when then script is done running, even if the script was invoked using the button.

The Script Language
QtScript is an implementation of ECMAScript and is currently documented here.
It is a C like syntax with the usual conditionals and loops. It also contains a bunch of math functions. The most obvious big difference is variables are untyped and are declared with “var”. Printing can be done with the print() function.

var i;
for (i=0; i<10; i++) {
	print(“Looping: “, i);
}

A few things that were added as extensions in rdla2exr.

clearStatusOverlay()
Clear all of the status lines
setStatusOverlay(row, message)
Set a status line with a message
setStatusOverlay(row)
Clear one status line

usleep(microseconds)
Wait for the specified number of microseconds

waitForPercentageDone()
waitForPercentageDone(percentage)
Without a parameter return the current value. With a parameter wait until at least the specified amount of the render (range 0 to 100) has finished and return the value.
waitForInstance()
waitForInstance(instance)
Without a parameter return the current render instance. With a parameter wait until the render instance is the given value or greater and return the value.
Scriptable buttons
startButton
stopButton
zoomOutButton
zoomInButton
zoomReset
prevOutputButton
nextOutputButton

Since the release is what cause the buttons to operate in the program to do their respective operations call the release() member on the buttons in the script.

startButton.release()
Scriptable selectors
lightSelector
aovSelector

The activation of the selector is what causes the selection even so selection is done buy called the activated() member on the selectors.

lightSelector.activated(0)

Sending New Light Colors
imageView.setNewColorSignal(red, green, blue)

