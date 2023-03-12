var i=0;
var instance=0;
var amount = 0.0;
while (i<10) {
    instance = waitForInstance();
    startTime = new Date();
    // zoomInButton.released();
    //stopButton.released();
    //startButton.released();
    pauseButton.released();
    instance = waitForInstance();
    pauseButton.released();
    endTime = new Date();
    delta = endTime - startTime;
    print("Elapsed microseconds:",delta.toString());
    i++;
}
