## Rendergraph Visualizer 

<img width="350" src="/resources/readme/rendergraph_visualizer.png" align="right" />

An interactive display for the current rendergraph state of the Island renderer.

## Keyboard Shortcuts

| Key          | Action 
|--------------|------------ 
|SPACE         |  Toggle Live/Playback state 
|LEFT  CURSOR  |  When in Playback state, move backward one frame 
|RIGHT CURSOR  |  When in Playback state, move forward  one frame 
|R             |  Reset Zoom and center the graph
|Z             |  Toggle magnifying glass
|F12           |  Toggle show / hide (deactivated when hidden)
|			   |
|Mouse Wheel   |  Zoom
|Mouse grab    |  Move Diagram


Note that the visualizer is stateful, and will respond to `windowExtent` events, if hooked up to the window event loop (see below)


## API Use 
	
1) Add a le::RendergraphVisualizer object to your app_o

	```cpp
	le::RendergraphVisualizer rendergraph_visualizer;
	```

2) In the app's update method, just before updating the rendergraph, call:
        
	```cpp
	app->rendergraph_visualizer.upate(rendergraph);
	```

3) Add to the ui event loop:

	```cpp
	app->rendergraph_visualizer.processAndFilterEvents(events.data(), &events_sz);
	events.resize(events_sz);
	```


You can see an example of how to use the rendergraph visualizer in [compute_example](/apps/examples/compute_example).
