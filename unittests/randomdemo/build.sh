rm ./randomdemo.{wasm,wast,abi}
../../build/tools/eosiocpp -o randomdemo.wast randomdemo.cpp
../../build/tools/eosiocpp -g randomdemo.abi randomdemo.cpp
