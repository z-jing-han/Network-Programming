# Project 2

# Part 1 Concurent Connection-Oriented Server

## header (np_simple.hpp)
Does nothing except `include "npshell.hpp"` and `#define MAX_CLIENT 30`

## source code (np_simple.hpp)

### main
Handles port (gets from argv, defaults to 7001 if none provided)

### concurentConnectionOrientServer
```=cpp
input argument : port(int)
return type    : void
```
A standard process: to handle multiple clients, use `fork()`
Parent continues to wait for `accept()` while child handles one task.

```
Server:
create socket -> set socket opt -> binding -> listen -> accept -> fork()
                                                         ∧ | ∧    / \
                                                        /  |  \  ∨   ∨
                                                       /   | parent child
                                                      /    ∨          /
Client:                                            send  getfd <----
```
`SO_REUSEADDR`:[How do SO_REUSEADDR and SO_REUSEPORT differ?](https://stackoverflow.com/questions/14388706/how-do-so-reuseaddr-and-so-reuseport-differ)
```=cpp
// create socket
server_fd = socket(AF_INET, SOCK_STREAM, 0);
// set socket opt: allows same IP to connect
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
// server address setting: set 3 fields including port
memset(&(server_addr.sin_zero), 0, 8);
// binding
bind(server_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));
// listen
listen(server_fd, MAX_CLIENT);
// zombie process
call sigchld_handler
// accept
while (true) {
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &sin_size);
    pid_t pid = fork();
    if (fork() == 0) {
        // child
        close(server_fd);
        
        // Read input from client_fd, write output to client_fd
        dup2(client_fd, STDIN_FILENO);
        dup2(client_fd, STDOUT_FILENO);
        dup2(client_fd, STDERR_FILENO);
        
        // Handle npshell
        npshellInit();
        npshellLoop();
        
        close(client_fd);
        exit(0);
    } else {
        // parent
        close(client_fd);
    }
}
```
The following two functions are in npshell.cpp, but are specific to Part 1 and similar to the original npshell, so they're included here.

### npshellInit
```=cpp
input argument : None
return type    : void
```
Register sigchld_handler, enter working directory and set `PATH="bin:."`.

### npshellLoop
```=cpp
input argument : None
return type    : void
```
Same as Project 1, except adds some NULLs argus for Part 2 shared functions

> 💡 **Info:**
> 
> So when Part 2 references Project 1, it focuses on why additional functions were needed

# Part 2

## header (np_single_proc.hpp)
Does nothing
```=cpp
#include "npshell.hpp"
constexpr int MAX_CLIENT = 40;
constexpr int FD_UNDEFINED = -1;
```

## source code (np_single_proc.cpp)

### main
Same as Part 1

### singleProcessConcurrentServer
```
input argument : port (int)
return type    : void
```
Before the accept loop, it's identical to Part 1—create socket, bind, listen
However, this is a `single process`, we **must handle each client fd separately**

[freeBSD FD man page](https://man.freebsd.org/cgi/man.cgi?query=FD_SET&apropos=0&sektion=3&manpath=FreeBSD+7-current&format=html)
```=cpp
/* Defined in <sys/select> */
FD_ZERO(&fdset);        // clear fd_set*
FD_SET(int fd, &fdset); // add fd to fdset
FD_ISSET(fd, &fdset);   // check if fd in fdset
FD_CLR(fd, &fdset);     // remove fd from fdset
```

Variable Table
```=cpp
// Stores all client fds
fd_set all_fd_set;
// Stores fdset before accepting new clients
fd_set rset;
```
Workflow:
1. Use `select()` to check if anyone has tasks
    `select(max_fd_history+1, &rset, NULL, NULL, (struct timeval *)0)`eturns number of fds with activity. Use this to determine loop count.
2. `accept()` to check for new clients
    If yes, do init (store in `User_Info_Map`)
    2-1. **Environment Variable** must be stored separately and restored per client
    2-2. Add to `fd_all_set`, but not to `rset` yet
    2-3. If `select()` return count already handled, go back to step 1
3. Loop through all client_fd_table to check who's active
    3-1. **How can we distinguish whether a client has just connected or is an existing client now trying to issue a command?**  Use `rset`, in `rset` means it's an existing client
    3-2. Setup environment variables
    3-3. Redirect input/output to client
    3-4. Handle command (one line only)
        3-4-1. f command is `exit`, log out and clean up
    3-5. Check if all active clients handled (by return of `select()`)
        3-5-1. If not, return to step 3
        3-5-2. If done, return to step 1


# Functional Handler (Extension From Project 1)

## header (npshell.hpp)

`pipeStruct`, `Info`  are same as in Project 1

### UserInfo
```=cpp
struct UserInfo {
    char IPAddress[INET_ADDRSTRLEN];
    int port;
    string UserName;
    map<string, string> EnvVar;
    int totalCommandCount;
    map<int, struct pipeStruct> pipeMap;
};
```
Stores state of each logged-in user
1. IPAddress: Stores IP(e.g., `127.0.0.1`, `140.113.190.87`)
2. Port: Stores port (same IP can have different ports `1024~65536`)
3. UserName: default = `"(no name)"`
4. EnvVar: Stores environment variables per user (mainly `PATH`，which determines which directories the user's shell will search when executing commands)
5. totalCommandCount: Same as in Project 1, but per user (can not be mixed with other client)
6. pipeMap: Also per user

## source code (npshell.cpp)

### User Pipe handler
global variable `map<pair<int,int>, pipeStruct> UserPipes`
1. Key: `UserPipes.first` is a `pair<int,int>`，indicating this pipe is from which User ID(`UserPipes.first.first`) to Which User ID(`User.first.second`) 
   > ⚠️ **Warning:**
   > 
   > -1 means user doesn't exist. Pipe still created and redirected to/from `/dev/null`
2. Value: `PipeStruct`, stores pipe info

### npshell_handle_one_line
```=cpp
input argument : User_Info_Map (map<int, UserInfo>&)
                 user_idx (const int)
                 exit (bool*)
                 client_fd_table (const int* const)
return type    : void
```
Handles the shell logic for a single line in the single-process (Part 2) version of the server.
(Environment variables are set before calling this)
Similar to Project 1 npshell loop, differences:
~~0. During testing, enter `"working_dirctory"` first~~
1. built-in function
   This function is rewritten mainly because of `setenv` and `printenv`，In the concurrent connection-oriented version, these can be handled in a `fork()`ed process.However, since we can't use `fork()` in the single-process version, we must handle them manually.
2. handle `exit`
   2-1. peviously, calling `exit()` simply broke out of the shell loop. Here, since only one line is processed at a time, we use an exit pointer argument to inform the main loop in singleProcessServer() to exit.
   2-2. Before exiting, user pipes must be properly cleaned up.
3. call `typePrompt()`的位置
   ~~cout is buffered I/O, so we previously forced it to flush with `cout.flush()`~~
   Now, it is called right after showing the welcome message and again after executing a command.

### broadcast
```=cpp
input argument : msg (string)
                 client_fd_table (const int* const)
                 User_Info_Map (const map<int, UserInfo>)
return type    : void
```
Outputs a message to all clients by iterating over the client FDs. (**This function does not restore output to the original fd; you must reset it manually afterward.**)
```=cpp
for (auto user : User_Info_Map) {
    if (client_fd_table[user.first] != -1) {
        dup2Client(client_fd_table[user.first]);
        cout << msg << endl;
    }
}
```

### dup2Client
Also does not reset output to the original fd.
```=cpp
void dup2Client(int fd) {
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
}
```

### builtInCommand_com_handle
A re-implementation of the built-in command handler to manage per-user environment variables.
In the single-process version, everything must be stored and handled inside `User_Info_Map`.
The rest behaves similarly to Project 1.
(`block` and `unblock` are explained in the demo section.)
New built-in commands like `who`, `name`, `tell`, `yell` are implemented according to the spec using `User_Info_Map`.

### ReConstructCommand
```=cpp
input agrument : info (const Info)
               : currentCommandStart (const int)
return type    : string
```
Used to rebuild the original command string for user-pipe broadcasting, since in Project 1 the full command string may have been tokenized and lost.
Uses `currentCommandStart` to determine which command segment to reconstruct.

### executeCommand
```=cpp
input argument : info (Info)
               : pipeMap (map<int, struct pipeStruct>&)   // Stored per user
               : currentCommandStart (const int)          // Stored per user
               : totalCommandCOunt (const int)            // Stored per user
               : ignore_idx (size_t*)                     // Stored per user
               : user_idx (const int)
               : User_Info_Map (const map<int, UserInfo>) // Stored per user
               : client_fd_table (const int* const)
return type    : void
```
This is arguably the most modified function. It passes in many arguments because each user needs their own isolated state.
(~~Technically, we could just pass `User_Info_Map`, but I didn’t want to refactor everything~~)

New functionality is mostly tied to the global variable:
```=cpp
map<pair<int,int>, struct pipeStruct> UserPipes;
```
`UserPipes.first` is a `pair<int,int>`, indicating a pipe from one user to another.
`-1` means the user does not exist, but **the command still executes.**
In this case, I/O is redirected to/from `/dev/null` (as per TA slides)
`UserPipes.second` is a `struct pipeStuct>` that stores the actual pipe.(only use `fd`)

1. Before executing, determine whether this command: 1. receives a user pipe `<n`  2. or sends a user pipe `>n`
   1-1. Check `info.argv[argvIndex]` for `<` or `>` characters. **Do not remove them yet**—they must remain until `fork()` and the child executes. he parent needs this info for broadcasting or error reporting.
   1-2. Determine pipe validity per the spec, and also **deteremine to create a pipe** even for invalid references.
       1-2-1. Receiving from another user:
           1-2-1-1. Invalid: Create pipe `{-1, user_idx}:pipe`
           1-2-1-2. Valid: Use existing pipe (no need to recreate)
       1-2-2. ending to another user:
           1-2-2-1. Invalid: Create pipe `{user_idx, -1}:pipe`
           1-2-2-2. Valid: Create pipe `{user_idx, to_user_idx}:pipe`
           
2. After `fork()` and before child execute command
    2-1. Adjust the order of `<` and `>`
    2-2. If receiving from another user, Use the corresponding pipe
        -1 then use `open("/dev/null", O_RDOONLY, 0)`
    2-3.  If sending to another user, Use the corresponding pipe
        -1 then use `open("/dev/null", O_RDWR, 0)`

3. After `fork()` parent close the related pipe
    3-1. If there exist pipe.first is -1, meaning that child must aleardy exe this command (not will be trouble in logic), directly remove it from pipe.

The rest of the logic is mostly the same as in Project 1.


### demo part

#### 1.
Real demo example (~~got the easilest one~~)
Implement `tell username msg`
Emm...jsut...iterate through `User_Info_Map`  to find a match where `info.argv[0][1]==user.second.UserName` (so then send)，if not then use `stoi` to catch ID
~~During Demo, I type `127.0.0.1` as `172.0.0.1` almost fail into emo demage.~~

#### 2.
Try practice `block` and `unblock` myself (~~Took 4 hours to write... how could anyone finish this in 10 minutes~~)
Custom Spec
1. If User A type `block B`
   1-1. B can not use `tell A msg` and command `command >A` to send msg to A
   1-2. If A type `commad <B` than consider no pipe exist (user not exist)
   1-3. A still can use `tell B msg` and `command >B` to send msg to B
   1-4. If B type `yell` A still can receive it.
       1-4-1 If want to implement A can not receive broadcast version, it should rewritten `broadcast function`, need to pass argument of callee to check whether is blocked.
   1-5. If A re type `block B`, than output Error

2. If A type `unblock B` before `block B`, than output Error

Example
```=bash
# User 1                       # User 2
% block 2            
                               % tell 1 test
                               *** Error: been block ***
                               % removetag test.html >1
                               *** Error: been block ***
% number <1
*** Error: pipe not exist. ***
% unblock 2
                               % tell 1 test
% *** 2 tell : test ***
                               % removetag test.html >1
*** receive command from 2 ***
% cat <2
...Some output...
```

How?
1. Extend built-in commands to include `block` and `unblock`
   1-1. Add a `vector<int> who_block_me` to `UserInfo` to record who blocked this user
        (This allows quick checks for whether this user should ignore a given user pipe, i.e., thowing result to `/dev/null`
        But in this way if I want to implement `block broadcast` it should be vice.)
   1-2. `find(User_Info_Map[target].who_block_me(), user_idx) == end?`
        To determine if user A blocked B
   1-3. Print error or update the block list based on the result

２. Before executing `command >n`, check is being blocked by `find(User_Info_Map[user_idx], n) == end?`, if so than treat the user pipe as invalid.

#### 3.
Another demo part: `firewall IP` (reference project 3 README.rd)
How?
1. Add variable `vector<string> firewall` to record what ip in firewall list
2. Add built-in command to deal with this variable
3. when login, check the ip is or not in firewall list

Bug: if it is been block, it will not exit right away, it would block until next command.

