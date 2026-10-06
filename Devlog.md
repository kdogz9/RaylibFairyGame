# Raylib Fairy game 

## Approach (150 words)

I chose to make a fairy platform game in Raylib since I wanted to create a small game that has replayability but also sticks within the style of games I want to create. I also wanted to keep it simple so I could learn more from this project. 

The main game loop is to collect the orbs to move onto the next level. I implemented moving platforms and power ups to add difficulty and vairation to the game to encourage replayability. I also implemented procedurally generated levels as well which increased with difficulty overtime so the player had a reason to go back to the game. 

As part of the requirement I decided to use a weather API to change the background of the game according to the weather in real life and I also wanted the weather to slightly impact on the players gravity and air drag movements. 


## Result (200 words)

The overall idea was successfully implemented. The player can jump onto the platforms and not go through them, they can collect the orbs which sends them to the next level and they can collect 3 different powerups which affects their jump height or adds feather falling to keep them in the sky longer. You can also manually change the weather background pressing the `L` key so players don't have to wait for it to change in real time. There is also background music that loops. 

However, during making the game I came across some issues such as the platforms generating above the screen which made it hard for the player to get the orb and see where they were. I had to adjust the code to add in boundaries so this stopped it from happening. I also had issues with the platforms generating either too close or too far from eachother so I had to find a mid ground point and debug this issue a lot. I also implemented my own assets from an artist and itch so the game wasn't being drawn as circles and sqaures to add character to it which meant preloading them into the emscripten build at the end so the browser could access them. 




## Reflection (186 words)

During this project, I became more comfortable using Raylib and C. I learned how procedural generation uses rules to prevent the platforms from spawning on top of each other. The platform generation checks the position of where the other platforms are so that they don't overlap.

 I also developed a better understanding of how velocity, gravity and frame time work together to create consistent jumping and falling. When the fairy jumps, its vertical velocity moves it upwards while gravity is applied every frame until the fairy starts falling again. Frame time is used so that this movement stays more consistent across different frame rates.

Another important thing I learned was how an API can be used as part of the actual gameplay rather than just displaying information. The weather API returns a weather code, which the game then uses to change the background and adjust the fairy's gravity depending on the conditions. Finally, I learned that creating a browser game involves additional steps such as using Emscripten to compile the game for the web and preloading assets so they can be accessed in the browser version.