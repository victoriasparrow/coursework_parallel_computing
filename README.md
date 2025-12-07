# Search Engine
![demo.http](/images/http.png)
![demo.tcp](/images/tcp.png)
### Dependencies

* OS: macOS, Linux; 
* Build system: CMake 3.20;
* Compiler: C++20 support is required: GCC 9+, Clang 13+.

### Copy the repository

```
git clone https://github.com/victoriasparrow/coursework_parallel_computing.git && cd repo
```

### How to build and run

```
mkdir build
cd build
cmake .. 
make 
```
1. Run the server:
```
./server
```

2. Run the TCP client:
```
./tcpclient
```

3. HTTP client: [http://localhost:8080](http://localhost:8080)