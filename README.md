# Timer
This is a timer module for the [Defold game engine](http://www.defold.com).

## Installation
You can use the Timer in your own project by adding this project as a [Defold library dependency](http://www.defold.com/manuals/libraries/). Open your game.project file and in the dependencies field under project add:

https://github.com/britzl/defold-timer/archive/master.zip

## Usage

    local id = timer.seconds(1.5, function(self, id)
      print("I will be called once, unless I'm cancelled")
    end)
    
    local id = timer.repeating(2.5, function(self, id)
      print("I will be called repeatedly, until cancelled")
    end)
    
    timer.cancel(id)
    timer.cancel_all()

## Limitations
The Timer module is available for all platforms that are currently supported by [Native Extensions](http://www.defold.com/manuals/extensions/).