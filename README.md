<p>
  High performance, offile, neural, web-based text-to-speech service, based on the <a href="https://github.com/rhasspy/piper" target="_blank">Piper</a> codebase and models. <br>
  Aimed at use in VoIP services, supports multithread mode, allows to preload and share models, supports output formats: MP3,WAV,L16, contains built-in resampler. <br>
  Requires: 
    <a href="https://onnxruntime.ai/" target="_blank">onnxruntime</a>, 
    <a href="https://github.com/espeak-ng/espeak-ng" target="_blank">espeak-ng</a>, 
    <a href="https://github.com/rhasspy/piper-phonemize" target="_blank">piper-phonemize</a>,  
    <a href="https://lame.sourceforge.io/" target="_blank">lame</a>, 
    <a href="https://github.com/xiph/speexdsp" target="_blank">speexdsp</a>, 
    <a href="https://github.com/akscf/wstk_c" target="_blank">wstk</a>
    <br>
 Voices models: <a href="https://github.com/rhasspy/piper/blob/master/VOICES.md" target="_blank">available here</a>
</p>

### Usage examples
```Bash
 # curl -v http://127.0.0.1:8802/v1/speech -X POST -H "Authorization: Bearer secret" -H "Content-Type: application/json; charset=utf-8" -d '{"language":"en","samplerate":8000,"format":"mp3","input":"Hello world!"}' --output result.mp3
 # curl -v http://127.0.0.1:8802/v1/speech -X POST -H "Authorization: Bearer secret" -H "Content-Type: application/json; charset=utf-8" -d '{"language":"en","samplerate":8000,"format":"wav","input":"Hello world!"}' --output result.wav
```

