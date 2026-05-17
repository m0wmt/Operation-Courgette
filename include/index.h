const char MAIN_page[] PROGMEM = R"=====(
<!DOCTYPE HTML><html>
<head>
  <title>Operation Courgette</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    p { font-size: 1.2rem;}
    body {  margin: 0;}
    .topnav { overflow: hidden; background-color: #50B8B4; color: white; font-size: 1rem; }
    .content { padding: 15px; }
    .card { background-color: white; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5); }
    .cards { max-width: 800px; margin: 0 auto; display: grid; grid-gap: 2rem; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); }
    .reading { font-size: 1.3rem; }
    .button { background-color: teal; border: none; color: white; padding: 10px 25px; text-decoration: none; font-size: 25px; cursor: pointer; margin: 3px; 
        border-radius: 5px; box-shadow:0 8px 16px 0 rgba(0,0,0,0.6)}
  </style>
</head>
<body>
  <div class="topnav">
    <h1>Operation Courgette</h1>
  </div>
  <div class="content">
    <div class="cards">
      <div class="card">
        <p style="color:rgb(10, 66, 64);">Last Update</p><p><span class="reading"><span id="rt">%RUNTIME%</span></span></p>
      </div>
      <div class="card">
        <p style="color:rgb(10, 66, 64);">Water Level</p><p><span class="reading"><span id="waterLevel">%LEVEL%</span></span></p>
      </div>
      <div class="card">
        <p style="color:rgb(10, 66, 64);">Irrigation State</p><p><span class="reading"><span id="irrigationState">%STATE%</span></span></p>
      </div>
    </div>

    <p></p>
    <p></p>
    <button class="button" onclick="toggleIrrigation()">Start/Stop Irrigation</button>

  </div>
<script>

  function toggleIrrigation() {
    var xhttp = new XMLHttpRequest();
    xhttp.open("GET", "/toggle", true);
    xhttp.send();
  }

  function updateIrrigationState(state) {
    document.getElementById("irrigationState").innerText = state;
  }

  function updateWATERLevel(level) {
    document.getElementById("waterLevel").innerText = level;
  }

  // Initial state update
  setInterval(function() {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
      if (this.readyState == 4 && this.status == 200) {
        updateIrrigationState(this.responseText);
      }
    };
    xhttp.open("GET", "/state", true);
    xhttp.send();
  }, 1000);

  if (!!window.EventSource) {
    var source = new EventSource('/events');
    
    source.addEventListener('waterlevel', function(e) {
      console.log("waterlevel", e.data);
      document.getElementById("waterLevel").innerHTML = e.data;
    }, false);

    source.addEventListener('runtime', function(e) {
      console.log("runtime", e.data);
      document.getElementById("rt").innerHTML = e.data;
    }, false);

    source.addEventListener('irrigationState', function(e) {
      console.log("irrigationState", e.data);
      document.getElementById("irrigationState").innerHTML = e.data;
    }, false);

}

</script>
</body>
</html>
)=====";