var i,
    instance=0,
    red = 0.0,
    green = 0.0,
    blue = 0.0,
    bigColor = 0;

while (true) {
    instance = waitForInstance();
    startTime = new Date();
    
    if (bigColor == 0) {
        red = 1.0;
    } else {
        red = Math.random();
    }
    
    if (bigColor == 1) {
        green = 1.0;
    } else {
        green = Math.random();
    }
    
    if (bigColor == 2) {
        blue = 1.0;
    } else {
        blue = Math.random();
    }

    setStatusOverlay(1, "Light Color");
    setStatusOverlay(2, "R: " + red.toFixed(2));
    setStatusOverlay(3, "G: " + green.toFixed(2));
    setStatusOverlay(4, "B: " + blue.toFixed(2));
    
    imageView.setNewColorSignal(red, green, blue);
    
    waitForInstance(instance + 1);
    waitForPercentageDone(2.0);
    
    if (bigColor == 2) {
        bigColor = 0;
    } else {
        bigColor += 1;
    }
    
    endTime = new Date();
    delta = endTime - startTime;
    print("Elapsed milliseconds:",delta.toString());
    setStatusOverlay(5,"Elapsed microseconds: " + delta.toString());
}
