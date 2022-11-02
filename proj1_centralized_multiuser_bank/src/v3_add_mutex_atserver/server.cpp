#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <unistd.h>
#include <stdio.h> 
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <thread>
#include <pthread.h>
#include <string>
#include <fstream>
#include <vector>

using namespace std;

#define PORT 8080
#define MAX_THREADS_AMOUNT 10
#define MAX_ACCOUNTS_AMOUNT 500
const char RECORDS_FILE_NAME[] = "Records.txt";


// A mutex for each client account
typedef struct s_ClientAccount
{
    // mutex cite: https://stackoverflow.com/questions/34100575/c-using-mutex-in-multithreaded-client-and-server
    int account_no; 
    pthread_mutex_t mutex; // the mutex that guards access to this client account  
    float balance;
    string name;
} t_ClientAccount;

struct s_ClientAccountTable
{
    s_ClientAccount* accounts[MAX_ACCOUNTS_AMOUNT]; // an array of pointers - not an array of instances
    int accounts_size; // size of items in accounts
};

// global instance
s_ClientAccountTable g_account_table;
pthread_mutex_t g_table_mutex; // guards access to g_account_table;


s_ClientAccount* find_or_create_account(int input_account_no, string input_account_name, float input_account_balance)
{
    s_ClientAccount* account = NULL;

    // acquire the global lock before accessing the global table
    pthread_mutex_lock(&g_table_mutex);

    // find the current account
    for (int i = 0; i < g_account_table.accounts_size; i++)
    {
        if (input_account_no == g_account_table.accounts[i]->account_no)
        {
             account = g_account_table.accounts[i];
        }
    }

    // not found in the table, so create a new one
    if (account == NULL)
    {
        s_ClientAccount new_account;
        new_account.account_no = input_account_no;
        new_account.name = input_account_name;
        new_account.balance = input_account_balance;
        pthread_mutex_init(&new_account.mutex, NULL);
        g_account_table.accounts[g_account_table.accounts_size] = &new_account;
        g_account_table.accounts_size++;
        cout << "A new account is created: No: " << input_account_no << "; Name: " << input_account_name << "; Balance: " << input_account_balance << ".\n";
    }

    // release the lock
    pthread_mutex_unlock(&g_table_mutex);
    return account;
}


// function for string delimiter
vector<string> split(string s, string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    string token;
    vector<string> res;

    while ((pos_end = s.find (delimiter, pos_start)) != string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (s.substr (pos_start));
    return res;
}

// initialize
void initialize_account(){
    g_account_table.accounts_size = 0;

    string line;
    ifstream recordsfile (RECORDS_FILE_NAME);

    if (recordsfile.is_open()){
        while ( getline(recordsfile, line) ){ //101 Peter 16000
            try{
                vector<string> v = split(line, " ");
                if (v.size() >= 3){
                    float balance = std::stof(v[2].c_str());
                    int account_no = std::stoi(v[0].c_str());
                    string name = v[1];
                    //create accounts 
                    find_or_create_account(account_no, name, balance);
                } else { 
                    cout << "Exception Caught, split result size: " << v.size() << ".\n";
                }
            }catch(std::exception const & e) {
                cout << "Exception Caught '" << e.what() << "'.\n"; 
            }
        }
        recordsfile.close();
    } else {
        cout << "Unable to open the '" << RECORDS_FILE_NAME << "' file, please check the name!\n"; 
        exit(0);
    }
}


void *client_connection_handler(void * socket){
    // get the socket descriptor
    int new_socket = *(int*)socket;

    // Setting up buffer for receiving msg
    char recv_buf[65536];
    memset(recv_buf, '\0', sizeof(recv_buf));

    while(recv(new_socket, recv_buf, sizeof(recv_buf), 0) > 0 ){
        printf("Server: recv from client tid(%ld): '%s' \n", pthread_self(), recv_buf);
        memset(recv_buf, '\0', strlen(recv_buf));
        break;
    }

    // valread = read( new_socket , buffer, 1024);
    // printf("%s\n",buffer );

    // sending a message to client
    const char *msg = "Hello from server";
    send(new_socket , msg , strlen(msg) , 0 );
    printf("Server: sent to client tid(%ld): '%s'\n", pthread_self(), msg);
   
}


 
int main(int argc, char *argv[]){
    // /* deal with input arguments*/
    // std::cout << "print arguments:\nargc == " << argc << '\n';
    // for(int ndx{}; ndx != argc; ++ndx) {
    //     std::cout << "argv[" << ndx << "] == " << argv[ndx] << '\n';
    // }
    // std::cout << "argv[" << argc << "] == "
    //           << static_cast<void*>(argv[argc]) << '\n';
    
    // intialize account information (create accounts)
    initialize_account();

    // Socket Cite: https://www.geeksforgeeks.org/socket-programming-cc/?ref=lbp
    int server_socket_fd, new_socket, valread;
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;    // IPv4
    server_address.sin_addr.s_addr = INADDR_ANY; // localhost
    server_address.sin_port = htons( PORT ); // 8080
    int opt = 1; // for setsockopt

    // Creating socket file descriptor (IPv4, TCP, IP)
    if ((server_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Server: socket failed");
        exit(EXIT_FAILURE);
    }
       
    // Optional: it helps in reuse of address and port. Prevents error such as: “address already in use”.
    if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("Server: setsockopt");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (bind(server_socket_fd, (struct sockaddr *)&server_address, 
                                 sizeof(server_address))<0)
    {
        perror("Server: bind failed");
        exit(EXIT_FAILURE);
    }

    // Putting the server socket in a passive mode, waiting for the client to approach the server to make a connection
    // The backlog=7, defines the maximum length to which the queue of pending connections for sockfd may grow. 
    // If a connection request arrives when the queue is full, the client may receive an error with an indication of ECONNREFUSED.
    if (listen(server_socket_fd, 7) < 0)
    {
        perror("Server: listen failed");
        exit(EXIT_FAILURE);
    }
    printf("Server: server is listening ...\n");


    // multi threads
    pthread_t thread_id[MAX_THREADS_AMOUNT]; // max 10 threads
    int thread_id_ctr = 0;

    while (1) { // block on accept() until positive fd or error
        struct sockaddr_in client_addr;
        socklen_t length = sizeof(client_addr);
        // Extracting the first connection request on the queue of pending connections for the listening socket (server_socket_fd) 
        // Creates a new connected socket, and returns a new file descriptor referring to that socket
        if ((new_socket = accept(server_socket_fd, (struct sockaddr *)&client_addr, 
                        (socklen_t*)&length))<0)
        {
            perror("Server: accept failed");
            exit(EXIT_FAILURE);
        }
        
        // converting the network address structure src in the af address family into a character string.
        char client_ip[INET_ADDRSTRLEN] = "";
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Server: new client accepted. client ip and port: %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        // creating a thread for each client
        if (pthread_create(&thread_id[thread_id_ctr], NULL, client_connection_handler, (void *) &new_socket) < 0)
        {
            perror("Server: create thread failed");
            exit(EXIT_FAILURE);
        }
        thread_id_ctr ++;
        printf("Server: new thread created with threadid: %ld\n", pthread_self());    
    }

    // waiting for the created thread to terminat
    //   pthread_join must be outside the pthread_create loop to create and exec multi threads parallelly. 
    //   if inside, each thread will be created and exec sequentially.
    for (int i = 0; i < MAX_THREADS_AMOUNT; i++){
        pthread_join(thread_id[i], NULL);
    }
    pthread_exit(NULL);

    printf("Server: server stopped. \n");
    close(server_socket_fd);
    return 0;
}