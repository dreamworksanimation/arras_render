var i=0;
var instance=0;
lightSelector.activated(0);
var amount = 0.0;
while (i<5) {
    instance = waitForInstance();
    startTime = new Date();
    // zoomInButton.released();
    imageView.setNewColorSignal(amount, 1.0-amount, 1.0);
    waitForInstance(instance + 1);
    waitForPercentageDone(50.0);
    endTime = new Date();
    delta = endTime - startTime;
    print("Elapsed microseconds:",delta.toString());
    i++;
    amount = amount + 0.2;
}
