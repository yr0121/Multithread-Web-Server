# Multithread-Web-Server
How to compile and run your program.
First, you need to make update the executable file.
And open the terminal in your working directory.
$./web_server [port number] [path] [number of dispatch] [number of worker thread] [dynamic
flag] [qlen][cache entries]
Then open the new windows to run the code in different ways.
$ Browser -- e.g. http://127.0.0.1:9000/filePath [The file should load in browser]
$ wget http://127.0.0.1:9000/filePath [The file should be downloaded]
$ cat ../urls | xargs -n 1 -P <num> wget
