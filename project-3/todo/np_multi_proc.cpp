#include "np_multi_proc.hpp"

using namespace std;

int UserInfo_fd, MSG_fd, UserPipeMatrix_fd, Firewall_fd[MAX_CLIENT];
struct UserInfo* UserInfo;
char* msg;
int *UserPipeMatrix;
char* firewall[MAX_CLIENT];
size_t user_idx_glob;

int main(int argc, char* argv[]) {
    
    int port = 7001;
    if (argv[1]) {
        port = stoi(string(argv[1]));
    }

    concurentConnectionOrientedServer(port);

    return 0;
}


void concurentConnectionOrientedServer(int port) {

    signal(SIGINT, release_share_memory);
    signal(SIGTERM, release_share_memory);
    signal(SIGUSR1, sendMsg);
    signal(SIGUSR2, receiveFifo);

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size;
    struct sigaction sa;

    // create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        cerr << "server socket error" << endl;
        exit(1);
    }

    // set socket opt
    int yes = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        cerr << "set sockopt error" << endl;
        exit(1);
    }

    // bind
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    memset(&(server_addr.sin_zero), 0, 8);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        cerr << "bind error" << endl;
        exit(1);
    }

    // listen
    if (listen(server_fd, MAX_CLIENT) == -1) {
        cerr << "listen error" << endl;
        exit(1);
    }

    // zombie processes
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        cerr << "sigaction error" << endl;
        exit(1);
    }

    // share memory
    UserInfo_fd = shm_open("UserInfo", O_CREAT | O_RDWR, 0666);
    MSG_fd = shm_open("msg", O_CREAT | O_RDWR, 0666);
    UserPipeMatrix_fd = shm_open("UserPipeMatrix", O_CREAT | O_RDWR, 0666);
    for (size_t i = 0; i < MAX_CLIENT; ++i) {
        string shm_name = "firewall[" + to_string(i) + "]";
        Firewall_fd[i] = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
        if (Firewall_fd[i] == -1) {
            cerr << "shmm_open error" << endl;
            exit(1);
        }
    }
    // Firewall_fd = shm_open("firewall", O_CREAT | O_RDWR, 0666);
    if (UserInfo_fd == -1 || MSG_fd == -1 || UserPipeMatrix_fd == -1) {
        cerr << "shmm_open error" << endl;
        exit(1);
    }
    // set size
    ftruncate(UserInfo_fd, MAX_CLIENT * sizeof(struct UserInfo));
    ftruncate(MSG_fd, MAX_COMMAND_SIZE);
    ftruncate(UserPipeMatrix_fd, MAX_CLIENT * MAX_CLIENT * sizeof(int));
    for (size_t i = 0; i < MAX_CLIENT; ++i) {
        ftruncate(Firewall_fd[i], INET_ADDRSTRLEN);
    }
    
    // map to memory
    UserInfo = (struct UserInfo *)mmap(0, MAX_CLIENT * sizeof(struct UserInfo), 
                                        PROT_READ | PROT_WRITE, MAP_SHARED, UserInfo_fd, 0);
    msg = (char *)mmap(0, MAX_COMMAND_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, MSG_fd, 0);
    UserPipeMatrix = (int *)mmap(0, MAX_CLIENT * MAX_CLIENT * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, UserPipeMatrix_fd, 0);
    for (size_t i = 0; i < MAX_CLIENT; ++i) {
        firewall[i] = (char *)mmap(0, INET_ADDRSTRLEN, PROT_READ | PROT_WRITE, MAP_SHARED, Firewall_fd[i], 0);
        memset(firewall[i], '\0', INET_ADDRSTRLEN);
    }
    
    // init share memory
    for (size_t i = 0; i < MAX_CLIENT; ++i) {
        UserInfo[i].client_fd = -1;
        UserInfo[i].pid = 0;
        UserInfo[i].port = 0;
        memset(UserInfo[i].IPAddress, '\0', sizeof(UserInfo[i].IPAddress));
        memset(UserInfo[i].UserName, '\0', NAME_SIZE);
    }

    memset(msg, '\0', MAX_COMMAND_SIZE);
    memset(UserPipeMatrix, -1, MAX_CLIENT * MAX_CLIENT * sizeof(int));
    /*
    for (size_t i = 0; i < MAX_CLIENT; ++i) {
        memset(firewall[i], '\0', INET_ADDRSTRLEN);
    }
    */
    // -2: block -1: unuse 0: enablue >0: keep fd from by open

    cout << "Server is listening on port:" << port << "..." << endl;

    // accept loop
    while (true) {

        sin_size = sizeof(struct sockaddr_in);
        if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
            cout << "accept error" << endl;
            continue;
        }

        size_t user_idx = 0;
        for (size_t i = 1; i < MAX_CLIENT; ++i) {
            if (UserInfo[i].client_fd == -1) {
                user_idx = i;
                user_idx_glob = i;
                break;
            }
        }
        
        pid_t pid = fork();
        if (pid < 0) {
            cerr << "Error: Unable to fork" << endl;
            exit(1);
        } else if (pid == 0) {
            close(server_fd);
            dup2Client(client_fd);
            close(client_fd);
            
            while(UserInfo[user_idx].client_fd == -1) {
                usleep(1000);
            }

            for (size_t i = 0; i < MAX_CLIENT; ++i) {
                if (strcmp(UserInfo[user_idx].IPAddress, firewall[i]) == 0) {
                    cerr << "*** Error: You can not login. ***" << endl;
                    UserInfo[user_idx].client_fd = -1;
                    UserInfo[user_idx].pid = 0;
                    UserInfo[user_idx].port = 0;
                    memset(UserInfo[user_idx].IPAddress, '\0', INET_ADDRSTRLEN);
                    memset(UserInfo[user_idx].UserName, '\0', NAME_SIZE);
                    exit(0);
                }
            }
            

            for (size_t i = 1; i < MAX_CLIENT; ++i) {
                if (UserInfo[i].client_fd != -1) {
                    UserPipeMatrix[user_idx * MAX_CLIENT + i] = 0;
                    UserPipeMatrix[i * MAX_CLIENT + user_idx] = 0;
                }
            }

            npshellInit();
            cout << "****************************************" << endl;
            cout << "** Welcome to the information server. **" << endl;
            cout << "****************************************" << endl;
            string welmsg = "*** User '" + string(UserInfo[user_idx].UserName) + 
                "' entered from " + UserInfo[user_idx].IPAddress + 
                ":" + to_string(UserInfo[user_idx].port) + ". ***";
            broadcast(welmsg);
            npshellLoop(user_idx);

            string leave_msg = "*** User '" + string(UserInfo[user_idx].UserName) + "' left. ***";
            broadcast(leave_msg);
            UserInfo[user_idx].client_fd = -1;
            UserInfo[user_idx].pid = 0;
            UserInfo[user_idx].port = 0;
            memset(UserInfo[user_idx].IPAddress, '\0', INET_ADDRSTRLEN);
            memset(UserInfo[user_idx].UserName, '\0', NAME_SIZE);

            char fifoName[NAME_SIZE];
            string fifo;
            for (size_t j = 1; j < MAX_CLIENT; ++j) {
                fifo = "user_pipe/"+to_string(j)+"-"+to_string(user_idx);
                strcpy(fifoName, fifo.c_str());
                unlink(fifoName);
                fifo = "user_pipe/"+to_string(user_idx)+"-"+to_string(j);
                strcpy(fifoName, fifo.c_str());
                unlink(fifoName);
                UserPipeMatrix[j * MAX_CLIENT + user_idx] = -1;
            }

            exit(0);
            
        } else {
            close(client_fd);
            UserInfo[user_idx].pid = pid;
            inet_ntop(AF_INET, &(client_addr.sin_addr), UserInfo[user_idx].IPAddress, INET_ADDRSTRLEN);
            UserInfo[user_idx].port = ntohs(client_addr.sin_port);
            strcpy(UserInfo[user_idx].UserName, "(no name)");
            UserInfo[user_idx].client_fd = client_fd;
        }
        
    }

}

void sigchld_handler(int signo) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void sendMsg(int signo) {
    if (signo == SIGUSR1) {
        cout << msg << endl;
    }
}

void receiveFifo(int signo) {
    if (signo == SIGUSR2) {
        for (size_t i = 1; i < MAX_CLIENT; ++i) {
            if (UserPipeMatrix[i * MAX_CLIENT + user_idx_glob] == -3) {
                char fifoName[NAME_SIZE];
                string fifo = "user_pipe/"+to_string(i)+"-"+to_string(user_idx_glob);
                strcpy(fifoName, fifo.c_str());
                int rfd = open(fifoName, O_RDONLY | O_NONBLOCK);
                if (rfd != -1) {
                    int fl = fcntl(rfd, F_GETFL, 0);
                    fcntl(rfd, F_SETFL, fl & ~O_NONBLOCK);
                }
                UserPipeMatrix[i * MAX_CLIENT + user_idx_glob] = rfd;
                // UserPipeMatrix[i * MAX_CLIENT + user_idx_glob] = open(fifoName, O_RDONLY | O_NONBLOCK);
            }
        }
    }
}

void release_share_memory(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        munmap(UserInfo, MAX_CLIENT * sizeof(struct UserInfo));
        munmap(msg, MAX_COMMAND_SIZE);
        munmap(UserPipeMatrix, MAX_CLIENT * MAX_CLIENT * sizeof(int));
        for (size_t i = 0; i < MAX_CLIENT; ++i) {
            munmap(firewall[i], INET_ADDRSTRLEN);
        }
        shm_unlink("UserInfo");
        shm_unlink("msg");
        shm_unlink("UserPipeMatrix");
        for (size_t i = 0; i < MAX_CLIENT; ++i) {
            string shm_name = "firewall[" + to_string(i) + "]";
            shm_unlink(shm_name.c_str());
        }
        exit(0);
    }
}

void npshellInit() {
    signal(SIGCHLD, sigchld_handler);
    signal(SIGPIPE, SIG_IGN);
    chdir("working_directory");
    setenv("PATH", "bin:.", 1);
}

void npshellLoop(const size_t user_idx) {
    int totalCommandCount = 0;
    size_t ignore_idx = 0;

    map<int, struct pipeStruct>  pipeMap;

    while (true) {
        Info myInfo = {false, {}, {}, {}};

        typePrompt(false);

        int commandNum = readCommand(myInfo, totalCommandCount);
        if (commandNum == -2) {
            break;
        } else if (commandNum == -1) {
            continue;
        }
        
        int builtInFlag = builtInCommand(myInfo, user_idx);

        int currentCommandStart = totalCommandCount;
        totalCommandCount += commandNum;
        
        for (size_t i = 0; i < myInfo.op.size(); ++i) {
            if (myInfo.op[i] == PIPE) {
                map<int, struct pipeStruct> tempMap;
                for (auto [key, value]:pipeMap) {
                    if (value.OutCommandIndex > currentCommandStart + (int)i) {
                        value.OutCommandIndex++;
                        tempMap[key+1] = value;
                    } else {
                        tempMap[key] = value;
                    }
                }
                pipeMap = tempMap;
                if (ignore_idx != 0 && (size_t)currentCommandStart + i <= ignore_idx) {
                    ignore_idx++;
                }
            } else if (myInfo.op[i] == IGNORE) {
                map<int, struct pipeStruct> tempMap;
                for (auto [key, value]:pipeMap) {
                    value.OutCommandIndex += myInfo.opOrder[i] - currentCommandStart;
                    tempMap[key + myInfo.opOrder[i] - currentCommandStart] = value;
                }
                pipeMap = tempMap;
            }
        }

        if (builtInFlag == -1) {
            break;
        } else if (!builtInFlag) {
            executeCommand(myInfo, pipeMap, currentCommandStart, totalCommandCount, &ignore_idx, user_idx);
        }
    }
}

// return value: (1) 1, built-in function, continue read next command, (2) -1, exit or ^C (3) 0, not built-in
int builtInCommand(Info info, const size_t user_idx) {

    // empty line or not single command
    if (info.argv[0].empty() || info.argv.size() != 1) {
        return 0;
    }

    // exit
    if (info.argv[0][0] == "exit") {
        return -1;
    }

    // setenv
    if (info.argv[0][0] == "setenv") {
        if (info.argv[0].size() == 3) {
            setenv(info.argv[0][1].c_str(), info.argv[0][2].c_str(), 1);
        } else {
            cerr << "command [setenv] lost parameter [var] or [value]" << endl;
        }
        return 1;
    }

    // printenv
    if (info.argv[0][0] == "printenv") {
        if (info.argv[0].size() == 2) {
            if (const char* env_p = getenv(info.argv[0][1].c_str())) {
                cout << env_p << endl;
            }
        }
        return 1;
    }

    // cd
    if (info.argv[0][0] == "cd") {
        if (chdir(info.argv[0][1].c_str()) < 0) {
            cerr << "Error: cd: " << info.argv[0][1] << ": No such file or directory\n";
        }
        return 1;
    }

    // who
    if (info.argv[0][0] == "who") {
        cout << "<ID>\t<nickname>\t<IP:port>\t<indicate me>" << endl;
        for (size_t i = 1; i < MAX_CLIENT; ++i) {
            if (UserInfo[i].client_fd != -1) {
                cout << i << "\t" << UserInfo[i].UserName << "\t" << UserInfo[i].IPAddress 
                     << ":" << UserInfo[i].port;
                if (i == user_idx) {
                    cout << "\t" << "<-me";
                }
                cout << endl;
            }
        }
        return 1;
    }

    // tell
    if (info.argv[0][0] == "tell") {
        int receiver = -1;
        for (size_t i = 0; i < MAX_CLIENT; ++i) {
            if (UserInfo[i].UserName == info.argv[0][1]) {
                receiver = (int)i;
            }
        }
        if (receiver == -1) {
            receiver = stoi(info.argv[0][1]);
        }
        
        if (UserInfo[receiver].client_fd == -1 || receiver >= MAX_CLIENT) {
            cerr << "*** Error: user #" << receiver << " does not exist yet. ***" << endl;
        }  else if (UserPipeMatrix[receiver * MAX_CLIENT + user_idx] == -2) {
            cerr << "*** Error: user #" << receiver << " block you. ***" << endl;
        } else {
            string tellmsg = "*** " + string(UserInfo[user_idx].UserName) + " told you ***:";
            for (size_t i = 2; i < info.argv[0].size(); ++i) {
                tellmsg += (" " + info.argv[0][i]);
            }
            memset(msg, '\0', MAX_COMMAND_SIZE);
            strcpy(msg, tellmsg.c_str());
            if (UserInfo[receiver].client_fd != -1) {
                kill(UserInfo[receiver].pid, SIGUSR1);
            }
        }
        return 1;
    }

    // yell
    if (info.argv[0][0] == "yell") {
        string yellMsg = "*** " + string(UserInfo[user_idx].UserName) + " yelled ***:";
        for (size_t i = 1; i < info.argv[0].size(); ++i) {
            yellMsg += (" " + info.argv[0][i]);
        }
        broadcast(yellMsg);
        return 1;
    }

    // name
    if (info.argv[0][0] == "name") {
        bool same_name_exist = false;
        for (size_t i = 1; i < MAX_CLIENT; ++i) {
            if (UserInfo[i].client_fd != -1 && string(UserInfo[i].UserName) == info.argv[0][1]) {
                cout << "*** User '" << UserInfo[i].UserName << "' already exists. ***" << endl;
                same_name_exist = true;
                break;
            }
        }
        if (!same_name_exist) {
            strcpy(UserInfo[user_idx].UserName, info.argv[0][1].c_str());
            string changeNameMsg = "*** User from " + string(UserInfo[user_idx].IPAddress) + ":"
                                 + to_string(UserInfo[user_idx].port) + " is named '" 
                                 + string(UserInfo[user_idx].UserName) + "'. ***";
            broadcast(changeNameMsg);
        }

        return 1;
    }
    
    if (info.argv[0][0] == "block" || info.argv[0][0] == "unblock") {
        int block_idx = stoi(info.argv[0][1]);
        if (UserInfo[block_idx].client_fd == -1 || block_idx >= MAX_CLIENT) {
            cerr << "*** Error: user #" << block_idx << " does not exist yet. ***" << endl;
        } else {
            if (info.argv[0][0] == "block" && UserPipeMatrix[user_idx * MAX_CLIENT + block_idx] == 0) {
                UserPipeMatrix[user_idx * MAX_CLIENT + block_idx] = -2;
            } else if (info.argv[0][0] == "block" && UserPipeMatrix[user_idx * MAX_CLIENT + block_idx] == -2) {
                cerr << "*** Error: user #" << block_idx << " is already blocked. ***" << endl;
            } else if (info.argv[0][0] == "unblock" && UserPipeMatrix[user_idx * MAX_CLIENT + block_idx] == 0) {
                cerr << "*** Error: user #" << block_idx << " does not be blocked by you. ***" << endl;
            } else if (info.argv[0][0] == "unblock" && UserPipeMatrix[user_idx * MAX_CLIENT + block_idx] == -2) {
                UserPipeMatrix[user_idx * MAX_CLIENT + block_idx] = 0;
            }
        }
        return 1;
    }

    if (info.argv[0][0] == "firewall") {
        for (size_t i = 0; i < MAX_CLIENT; ++i) {
            if (strstr(firewall[i], ".") == NULL) {
                strcpy(firewall[i], info.argv[0][1].c_str());
                break;
            }
        }
        return 1;
    }
    
    if (info.argv[0][0] == "unfirewall") {
        for (size_t i = 0; i < MAX_CLIENT; ++i) {
            if (strcmp(firewall[i], info.argv[0][1].c_str()) == 0) {
                memset(firewall[i], '\0', INET_ADDRSTRLEN);
                break;
            }
        }
        return 1;
    }

    return 0;
}

void typePrompt(bool showPath) {
    struct passwd *pw;
    char hostname[MAX_SIZE], cwd[MAX_SIZE];

    pw = getpwuid(getuid());
    gethostname(hostname, MAX_SIZE);
    getcwd(cwd, MAX_SIZE);
    if (showPath) {
        cout << pw->pw_name << "@" << hostname << ":~" << cwd << "$ ";
    } else {
        cout << "% ";
    }
    cout.flush();
}

int readCommand(Info &info, const int totalCommandCount) {
    vector<vector<string>> tempArgv;
    string command;

    if (!getline(cin, command)) {
        return -1;
    }

    istringstream iss(command);
    string token;
    int command_size = 0;
    
    tempArgv.push_back({});
    
    bool is_tell_or_yell = false, is_first_line_first_word = true;

    while (iss >> token) {
        if (is_first_line_first_word  && (token == "yell" || token == "tell")) {
            is_tell_or_yell = true;
        }
        is_first_line_first_word = false;

        if (token == "&") {
            info.bg = true;
        } else if (!is_tell_or_yell && (token == ">" || token[0] == '|' || token[0] == '!' || token[0] == '%')) {
            info.op.push_back(
                (token == ">" ? OUT_RD: 
                    (token == "|" ? PIPE:
                        (token[0] == '|'? NUM_PIPE:
                            (token[0] == '!'? NUM_PIPE_ERR:
                                IGNORE    
                            )
                        )
                    )
                )
            );
            int opOrder = 0;
            size_t prev_start = 1;
            for(size_t i = 1; i < token.size(); ++i) {
                if (token[i] == '+') {
                    opOrder += stoi(token.substr(prev_start, i - prev_start));
                    prev_start = i + 1;
                }
            }
            if (token.size() != 1) {
                opOrder += stoi(token.substr(prev_start, token.size() - prev_start));
            }
            info.opOrder.push_back(
                (token == ">" ? NOT_PIPE : totalCommandCount + command_size + 
                    (token.size() == 1 ? NOT_NUMBER_PIPE:opOrder))
            );
            command_size++;
            tempArgv.push_back({});
        } else {
            tempArgv[command_size].push_back(token);
        }

        // Fix number_pipe output
        if (!is_tell_or_yell && token == "|") {
            for (size_t i = 0; i < info.opOrder.size(); ++i) {
                if ((info.op[i] == NUM_PIPE || info.op[i] == NUM_PIPE_ERR || info.op[i] == IGNORE) && (int)i + info.opOrder[i] < totalCommandCount + command_size + 1) {
                    info.opOrder[i]++;
                }
            }
        }
    }
    
    
    if (tempArgv[0].empty()) {
        return -2;
    }
    
    for (size_t i = 0; i < info.op.size(); ++i) {
        if (info.op[i] && tempArgv[i+1].empty()) {
            //cerr << "Error: Syntax error near unexpected token 'newline'\n";
            //return -1;
            tempArgv.erase(tempArgv.begin() + i + 1);
            break;
        }
    }
    
    info.argv = tempArgv;

    if (info.argv.size() != info.op.size()) {
        info.op.push_back(END_OF_COMMAND);
        info.opOrder.push_back(NOT_PIPE);
    }
    return (int)info.argv.size() - (info.op.size() > 1 && info.op[info.op.size()-2] == OUT_RD  ? 1:0);
}

string ReConstructCommand(const Info info, const int currentCommandStart) {
    string realCommandStr = "";
    for (size_t i = 0; i < info.argv.size(); ++i) {
        for (size_t j = 0; j < info.argv[i].size(); ++j) {
            realCommandStr += ((j == 0 ? "":" ") + info.argv[i][j]);
        }

        if (info.op[i] != END_OF_COMMAND) {
            switch(info.op[i]) {
                case PIPE:
                    realCommandStr += " | ";
                    break;
                case NUM_PIPE:
                    realCommandStr += (" |" + to_string(info.opOrder[i] - currentCommandStart));
                    if (i != info.argv.size() - 1) {
                        realCommandStr += " ";
                    }
                    break;
                case NUM_PIPE_ERR:
                    realCommandStr += (" !" + to_string(info.opOrder[i] - currentCommandStart));
                    if (i != info.argv.size() - 1) {
                        realCommandStr += " ";
                    }
                    break;
                case OUT_RD:
                    realCommandStr += " < ";
                    break;
                default:
                    break;
            }
        }
    }
    
    return realCommandStr;
}

void executeCommand(Info info, map<int, struct pipeStruct>& pipeMap, const int currentCommandStart, 
                    const int totalCommandCount, size_t *ignore_idx, const size_t user_idx) {
    int status;

    for (size_t i = (size_t)currentCommandStart; i < (size_t)totalCommandCount; ++i) {
        if (*ignore_idx != 0 && i <= *ignore_idx) {
            continue;
        }
        
        size_t argvIndex = i - (size_t)currentCommandStart;
        
        if (info.op[argvIndex] == IGNORE) {
            *ignore_idx = info.opOrder[argvIndex];
        }

        int from_user_pipe = -1, to_user_pipe = -1;
        size_t from_token_idx = 0, to_token_idx = 0;
        for (size_t j = 0; j < info.argv[argvIndex].size(); ++j) {
            if (info.argv[argvIndex][j].size() != 1 && info.argv[argvIndex][j][0] == '<') {
                from_user_pipe = stoi(info.argv[argvIndex][j].substr(1, info.argv[argvIndex][j].size() - 1));
                from_token_idx = j;
            }
            if (info.argv[argvIndex][j].size() != 1 && info.argv[argvIndex][j][0] == '>') {
                to_user_pipe = stoi(info.argv[argvIndex][j].substr(1, info.argv[argvIndex][j].size() - 1));
                to_token_idx = j;
            }
        }

        if (from_user_pipe != -1) {
            if (UserInfo[from_user_pipe].client_fd == -1 || from_user_pipe >= MAX_CLIENT) {
                cout << "*** Error: user #" << from_user_pipe << " does not exist yet. ***" << endl;
                from_user_pipe = -1;
            } else if (UserPipeMatrix[from_user_pipe * MAX_CLIENT + user_idx] == 0) {
                cout << "*** Error: the pipe #" << from_user_pipe << "->#" << user_idx 
                     << " does not exist yet. ***" << endl;
                from_user_pipe = -1;
            } else {
                string pipe_in_msg = "*** " + string(UserInfo[user_idx].UserName)
                                     + " (#" + to_string(user_idx)+ ") just received from " 
                                     + string(UserInfo[from_user_pipe].UserName) + " (#" + to_string(from_user_pipe) + ") by '";
                pipe_in_msg += ReConstructCommand(info, currentCommandStart);                
                pipe_in_msg += "' ***";
                broadcast(pipe_in_msg);
            }
        }

        if (to_user_pipe != -1) {
            if (UserInfo[to_user_pipe].client_fd == -1 || to_user_pipe >= MAX_CLIENT) {
                cout << "*** Error: user #" << to_user_pipe << " does not exist yet. ***" << endl;
                to_user_pipe = -1;
            } else if (UserPipeMatrix[user_idx * MAX_CLIENT + to_user_pipe] > 0) {
                cout << "*** Error: the pipe #" << user_idx << "->#" << to_user_pipe << " already exists. ***" << endl;
                to_user_pipe = -1;
            } else if (UserPipeMatrix[to_user_pipe * MAX_CLIENT + user_idx] == -2) {
                cerr << "*** Error: user #" << to_user_pipe << " block you. ***" << endl;
                to_user_pipe = -1;
            } else {
                // normal case
                string pipe_out_msg = "*** " + string(UserInfo[user_idx].UserName)
                                      + " (#" + to_string(user_idx) + ") just piped '";
                pipe_out_msg += ReConstructCommand(info, currentCommandStart);
                pipe_out_msg += ("' to " + string(UserInfo[to_user_pipe].UserName) + " (#" + to_string(to_user_pipe) + ") ***");
                broadcast(pipe_out_msg);
            }
        }

        if (info.op[argvIndex] != OUT_RD) {
            if ((info.op[argvIndex] == PIPE || info.op[argvIndex] == NUM_PIPE || info.op[argvIndex] == NUM_PIPE_ERR) && 
            pipeMap.find(info.opOrder[argvIndex]) == pipeMap.end()) {
                pipeMap[info.opOrder[argvIndex]] = {info.opOrder[argvIndex], {}, {}, info.op[argvIndex]};
                if (pipe(pipeMap[info.opOrder[argvIndex]].fd) < 0) {
                    cerr << "Error: Unable to create pipe" << endl;
                    exit(1);
                }
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            cerr << "Error: Unable to fork" << endl;
        } else if (pid == 0) {

            if (from_token_idx != 0) {
                info.argv[argvIndex].erase(info.argv[argvIndex].begin() + from_token_idx);
            }
            if (to_token_idx != 0) {
                info.argv[argvIndex].erase(info.argv[argvIndex].begin() + to_token_idx);
            }
            
            if (pipeMap.find((int)i) != pipeMap.end()) {
                close(pipeMap[(int)i].fd[1]);
                dup2(pipeMap[(int)i].fd[0], STDIN_FILENO);
                close(pipeMap[(int)i].fd[0]);
            } else if (from_user_pipe == -1 && from_token_idx != 0) {
                dup2(open("/dev/null", O_RDONLY, 0), STDIN_FILENO);
            } else if (from_user_pipe != -1) {
                if (UserPipeMatrix[from_user_pipe * MAX_CLIENT + user_idx] > 0) {
                    dup2(UserPipeMatrix[from_user_pipe * MAX_CLIENT + user_idx], STDIN_FILENO);
                    close(UserPipeMatrix[from_user_pipe * MAX_CLIENT + user_idx]);
                    UserPipeMatrix[from_user_pipe * MAX_CLIENT + user_idx] = 0;
                } else {
                    dup2(open("/dev/null", O_RDONLY, 0), STDIN_FILENO);
                }
            }

            if (info.op[argvIndex] == OUT_RD) {
                int fd;
                fd = open(info.argv[argvIndex+1][0].c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (fd < 0) {
                    cerr << "Error: " << info.argv[argvIndex+1][0] << ": Failed to open or create file\n";
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            } else if (info.op[argvIndex] == PIPE || info.op[argvIndex] == NUM_PIPE || info.op[argvIndex] == NUM_PIPE_ERR) {
                close(pipeMap[info.opOrder[argvIndex]].fd[0]);
                if (info.op[argvIndex] == NUM_PIPE_ERR) {
                    dup2(pipeMap[info.opOrder[argvIndex]].fd[1], STDERR_FILENO);
                }
                dup2(pipeMap[info.opOrder[argvIndex]].fd[1], STDOUT_FILENO);
                close(pipeMap[info.opOrder[argvIndex]].fd[1]);
            } else if (to_user_pipe == -1 && to_token_idx != 0) {
                dup2(open("/dev/null", O_RDWR, 0), STDOUT_FILENO);
            } else if (to_user_pipe != -1) {
                string fifo = "user_pipe/"+to_string(user_idx)+"-"+to_string(to_user_pipe);
                char fifoName[NAME_SIZE];

                strcpy(fifoName, fifo.c_str());
                mkfifo(fifoName, 0666);
                UserPipeMatrix[user_idx * MAX_CLIENT + to_user_pipe] = -3;
                kill(UserInfo[to_user_pipe].pid, SIGUSR2);
                int fd = open(fifoName, O_WRONLY);
                
                if (fd == -1) {
                    dup2(open("/dev/null", O_RDWR, 0), STDOUT_FILENO);
                } else {
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
            } 
            
            vector<char*> args;
            for (auto &arg : info.argv[argvIndex]) {
                args.push_back(arg.data());
            }
            args.push_back(nullptr);

            if (execvp(args[0], args.data()) == -1) {
                cerr << "Unknown command: [" << args[0] << "]." << endl;
                exit(1);
            }
        } else {
            if (pipeMap.find((int)i) != pipeMap.end()) {
                pipeMap[(int)i].relate_pids.push_back(pid);
            }

            if (pipeMap.find((int)i) != pipeMap.end()) {
                close(pipeMap[(int)i].fd[0]);
                close(pipeMap[(int)i].fd[1]);
            }
            
            if (((info.op[argvIndex] == END_OF_COMMAND && to_user_pipe == -1) || info.op[argvIndex] == OUT_RD) && pipeMap.find((int)i) != pipeMap.end()) {
                for (pid_t pid_i: pipeMap[(int)i].relate_pids) {
                    waitpid(pid_i, &status, 0);
                }
            } else if ((info.op[argvIndex] == END_OF_COMMAND && to_user_pipe == -1) || info.op[argvIndex] == OUT_RD || info.op[argvIndex] == IGNORE) {
                waitpid(pid, &status, 0);
            }

            if (from_user_pipe != -1 && from_token_idx != 0) {
                string fifo = "user_pipe/"+to_string(from_user_pipe)+"-"+to_string(user_idx);
                char fifoName[NAME_SIZE];
                strcpy(fifoName, fifo.c_str());
                unlink(fifoName);
            } 
        }
    }
    while (waitpid(-1, &status, WNOHANG) > 0);
}

void broadcast(string broadcastMsg) {
    memset(msg, '\0', MAX_COMMAND_SIZE);
    strcpy(msg, broadcastMsg.c_str());
    for (size_t i = 1; i < MAX_CLIENT; ++i) {
        if (UserInfo[i].client_fd != -1) {
            kill(UserInfo[i].pid, SIGUSR1);
        }
    }
    usleep(1000);
}

void dup2Client(int fd) {
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
}