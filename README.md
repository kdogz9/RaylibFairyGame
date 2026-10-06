# RaylibFairyGame

Fairy sky glade is a micro platfrom game where you play as a fairy and collect orbs on moving platforms to move to the next level. There are powerups which provide jump boosts and feather falling to help make the game easier and each level gets progressively harder. 

## API integregation

This game uses real time weather data from the Open-Meteo forecast API. The live weather code changes the background of the game and impacts the air drag movement and gravity of the fairy.  

Bright sunny backgrounds in the game means that the game plays normally and with a slight affect on the gavity or air drag of the player. 

If it is raining however, there is a slight downwards acceleration added to the player to simulate the affects of the weather in real life. 

[API Link](https://open-meteo.com/)

[Open-Meteo Weather API Endpoint](https://api.open-meteo.com/v1/forecast?latitude=51.5074&longitude=-0.1278&current=weather_code) - Exact JSON response used in the game. 

## How to compile with Emscripten 

To compile with Emscripten, first check the Emscripten environment variables are loaded:

``` 
bash

source /workspaces/emsdk/emsdk_env.sh
```

Then run the web assembly compilation command: 

```
bash

emcc -o index.html src/raylib_game.c -Os -Wall \
  -I/workspaces/raylib/src /workspaces/raylib/src/libraylib.web.a \
  -s USE_GLFW=3 -s ASYNCIFY -s FETCH=1 \
  --preload-file fairy.png \
  --preload-file Platform.png \
  --preload-file Diamond.png \
  --preload-file Star.png \
  --preload-file Feather.png \
  --preload-file BackgroundMusic.mp3
  ```
  This is the one I used as I added in my own assets and music to the game so I had to ensure they compiled inside the html files. 

  This then produces the `index.html` , `index.js` , `index.wasm` and `index.data` needed to run the game. 

## How to serve and play in a browser 

After the game has been compiled, download `Python` and run this line in the terminal: 

```
Bash 

python3 -m http.server 8000
```

Open `http://localhost:8000` in the browser after running the server.