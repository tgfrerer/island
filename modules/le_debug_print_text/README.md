# Debug Print Text

## A stateful debug printer for debug messages

The simplest way to print to screen is to use the Global Printer
Singleton -- it is available to any file that includes this header.

You can print like this:

```cpp
le::DebugPrint("I'm printing %04d", 1);
```

The debug printer supports `printf` formatting.

To see the messages rendered on top of a specific renderpass,
you must do this:

```cpp
le::DebugPrint::drawAllMessages(main_renderpass);
```

Otherwise, all messages are rendered into the last renderpass that is
part of the rendergraph, assuming that this renderpass is going to the
screen.

`drawAllMessages()` clears the message state and resets the debug 
message printer.

Note that there is no implicit synchronisation - this printer is
unaware that other threads might be using it.

The cursor moves with text that has been printed, and there is support
for multi-line text: If a newline `\n` character is detected, the
cursor moves to the next line.

You can set colour style information; Style info is on a stack to which
you can push / pop. Yes, this is stateful. It is also concise and
relatively simple (assuming a single-threaded environment) we might
want to reconsider this architecture if we get into trouble with
threading.

On draw, all the text that has accumulated through the frame gets
printed in one go, and the accumulated print instructions reset.

# Acknowledgements

* Pixel font data based on the free
[Tamsyn](http://www.fial.com/~scott/tamsyn-font/) bitmap font

