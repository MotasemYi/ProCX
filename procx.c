#include <stdio.h>
#include <stdlib.h>    
#include <unistd.h>    
#include <sys/types.h>  
#include <sys/wait.h>  
#include <string.h>    
#include <time.h>      
#include <signal.h>    
#include <errno.h>
#include <unistd.h>

#include <sys/ipc.h>    
#include <sys/shm.h>    
#include <sys/msg.h>    

#include <semaphore.h>  
#include <fcntl.h>    

#include <pthread.h>    



// process mode için enum
typedef enum
{
    ATTACHED = 0,
    DETACHED = 1
} ProcessMode;

// process statusu için enum
typedef enum
{
    RUNNING = 0,
    TERMINATED = 1
} ProcessStatus;

// ipv Commandi için enum
// diğer instanclara göndermek istediğimiz mesajin türü 
// 1 process başlatıldı , 2 process userderden  , 3 process userden sonlanmayan 
typedef enum
{
    CMD_START = 1,
    CMD_TERMINATE = 2,
    CMD_STATUS_CHANGE = 3
} IPCCommand;

// ProcessInfo struct
typedef struct
{
        pid_t pid;
                         
    pid_t owner_pid;
                   
    char command[256];
                 
    ProcessMode mode;
                  
    ProcessStatus status;
              
    time_t start_time;
                 
    int is_active;
                     
} ProcessInfo;

// Shared Memory structı
typedef struct
{
        ProcessInfo processes[50];
         
    int process_count;
                 
} SharedMemory;

// Mesaj structı
typedef struct
{
    long msg_type;
    IPCCommand command;
    pid_t sender_pid;
    pid_t target_pid;
             
} Message;


// bu global variableleri projemizin bütün işlemleri ile bağlıyoruz
// bu şekilde bütün fonksiyonlar processler mainde yada mainde değilse projenin bütün işlemleri ulaşabilecek  

//fonksiyonlarla oluştutcağımım sistem componentleri global variableleri bağlayacağım
// global shared memory pointeri yani bellekteki shared memory alanına bir pointer
SharedMemory *g_shared = NULL;

// global shm id (detach için tutuyorum)
int g_shmid = -1;

// global semaphore 
sem_t *g_sem = NULL;

// global monitor thread
volatile int g_monitor_running = 1;
pthread_t g_monitor_tid;
pid_t g_self_pid = -1;

// global message queue + receiver thread
int g_msgid = -1;
volatile int g_receiver_running = 1;
pthread_t g_receiver_tid;


// main menu göstermek için 
void print_menu()
{
        printf("\n");
        printf("┌────────────────────────────────────\n");
        printf("│            ProCX                  │\n");
        printf("├───────────────────────────────────┤\n");
        printf("│ 1. Yeni Program Çalıştır          │\n");
        printf("│ 2. Çalışan Programları Listele    │\n");
        printf("│ 3. Program Sonlandır              │\n");
        printf("│ 0. Çıkış                          │\n");
        printf("└───────────────────────────────────┘\n");
        printf("Seçiminiz: ");
}

//  statustan stringe
const char *status_to_string(ProcessStatus st)
{
        return (st == RUNNING) ? "RUNNING" : "TERMINATED";
}

// command parsing için yardımcı fonksiyon
int parse_command(char *input, char *argv[], int max_args)
{
        int argc = 0;

        // sondaki \n temizle
    size_t n = strlen(input);
        if (n > 0 && input[n - 1] == '\n') input[n - 1] = '\0';

        char *token = strtok(input, " ");
        while (token != NULL && argc < max_args - 1)
    {
                argv[argc++] = token;
                token = strtok(NULL, " ");
           
    }
        argv[argc] = NULL;
        return argc;
}



void MakeSharedMemory()
{
    // ftok benzersiz bir anahtar oluşturmak için ftok kullanılır
    // yani bütün instancler aynı shared memory kullanacak
    key_t key = ftok("shmfile", 65);
        if (key == -1)
    {
                perror("ftok hatasi");
                exit(1);
           
    }

        int first_creator = 0;

        // ilk defا oluşturmak
    g_shmid = shmget(key, sizeof(SharedMemory), 0666 | IPC_CREAT | IPC_EXCL);
        if (g_shmid != -1)
    {
        first_creator = 1;
           
    }
    else
    {
        if (errno == EEXIST)
        {
            // Daha önce oluşturulmuşsa sadece eriş
            g_shmid = shmget(key, sizeof(SharedMemory), 0666);
                        if (g_shmid == -1)
            {
                perror("shmget existing hatasi");
                exit(1);
                            
            }
                    
        }
        else
        {
            perror("shmget hatasi");
            exit(1);
                    
        }
           
    }

    // shmat ile shared memory programin adresi alanına bağlıyor
    g_shared = (SharedMemory *)shmat(g_shmid, NULL, 0);
        if (g_shared == (void *)-1)
    {
                perror("shmat hatasi");
                exit(1);
           
    }

        if (first_creator)
    {
                g_shared->process_count = 0;
                for (int i = 0; i < 50; i++)
        {
                        g_shared->processes[i].is_active = 0;
                    
        }
                printf("shared memory ilk kez oluşturuldu ve init edildi.\n");
           
    }
    else
    {
                printf("shared memory daha önce olusturuldu \n");
           
    }
}

// program bitince shared memory'i bırakmak için
void cleanup_shared_memory()
{
        if (g_shared != NULL)
    {
                shmdt(g_shared);
          // detach
        g_shared = NULL;
           
    }
}

// race condotion engellemek için semafor kullanacağım
// seamfor oluşturumak için MakeSemaphore()
// semaforu kullanmak için iki metot lock_shared() , unlock_shared()
void MakeSemaphore()
{
    // bütün processler aynı semaforu kullanacak
    const char *sem_name = "/procx_sem";
    int first_creator = 0;

    // ilk defا oluşturuyorsa 
    g_sem = sem_open(sem_name, O_CREAT | O_EXCL, 0666, 1);
    
    if (g_sem != SEM_FAILED){
        first_creator = 1;   
    }
    else
    {
        if (errno == EEXIST){
            // daha önce oluşturulmuşsa sadece eriş
            g_sem = sem_open(sem_name, O_CREAT, 0666, 1);
            
            if (g_sem == SEM_FAILED){
                perror("sem_open existing hatasi");
                exit(1);
                            
            }
                    
        }
        else
        {
            perror("sem_open hatasi");
            exit(1);
                    
        }
           
    }

        if (first_creator)
    {
        printf("semafor ilk kez oluşturuldu.\n");
           
    }
    else
    {
        printf(" semafor daha önce olusturuldu.\n");
           
    }
}

// semafor lock 1 den 0
void lock_shared()
{
        if (g_sem == NULL) return;
        if (sem_wait(g_sem) == -1)
    {
        perror("sem_wait hatasi");
        exit(1);
           
    }
}

// semafor unlock 0 den 1
void unlock_shared()
{
        if (g_sem == NULL) return;
        if (sem_post(g_sem) == -1)
    {
        perror("sem_post hatasi");
        exit(1);
           
    }
}

// program bittiğinde semaforu kapatmak için
void cleanup_semaphore()
{
        if (g_sem != NULL)
    {
        sem_close(g_sem);
        g_sem = NULL;
           
    }
}

void MakeMessageQueue()
{
    // Yeni mesaj kuruk oluşturma
    key_t key = ftok("shmfile", 99);
        if (key == -1)
    {
        perror("ftok msg key hatasi");
        exit(1);
           
    }

        // Daha önce oluştrulumuşsa sadece eriş
    g_msgid = msgget(key, 0666 | IPC_CREAT);
        if (g_msgid == -1){
            perror("msgget hatasi");
            exit(1);
    }

        printf("Message kuyruğu hazir.\n");
}

// Message Queueyu temizler yani program bitince sistemden siler
void cleanup_message_queue()
{
        if (g_msgid != -1){
        // Kontrol amaçlı: msgctl(g_msgid, IPC_RMID, NULL);
        if (msgctl(g_msgid, IPC_RMID, NULL) == -1)
        {
            // Hata yoksayılabilir eğer başka bir instance temizlediyse
        }
            g_msgid = -1;           
    }
}

// ipc message kuyruğa mesai gönderir
void send_ipc_message(IPCCommand cmd, pid_t target_pid)
{
    if (g_msgid == -1)
        return;

    Message msg;
    msg.msg_type = 1; // Tüm mesajlar için tip 1
    msg.command = cmd;
    msg.sender_pid = g_self_pid;
    msg.target_pid = target_pid;

    // msg_sz = sizeof(Message) - sizeof(long)
    if (msgsnd(g_msgid, &msg, sizeof(Message) - sizeof(long), IPC_NOWAIT) == -1)
    {
        // kuyruk doluysa geç
    }
}

// burda A process dan kuyruğa gönderdiğimiz mesaj B process kuyrukt alıp okuyacak
void *receiver_thread_func(void *arg)
{
        (void)arg;

        while (g_receiver_running)
    {
        Message msg;
        // msg_sz = sizeof(Message) - sizeof(long)
        ssize_t r = msgrcv(g_msgid, &msg, sizeof(Message) - sizeof(long), 1, IPC_NOWAIT);
            if (r > 0){
                if (msg.sender_pid != g_self_pid)
            {
                // mesaj türü göre ekrana bild. yazdrırıyoruz
                const char *cmd_str = "Bilinmeyen Komut";
                switch (msg.command)
                {
                case CMD_START:
                    cmd_str = "Yeni Process Başlatıldı";
                    break;
                case CMD_TERMINATE:
                    cmd_str = "Process Sonlandırıldı (User)";
                    break;
                case CMD_STATUS_CHANGE:
                    cmd_str = "Process Sonlandı (Monitor)";
                    break;
                }
                    printf("\n[IPC] Instance PID %d'den Bildirim: %s (PID: %d)\n",
                    msg.sender_pid, cmd_str, msg.target_pid);
                    printf("Seçiminiz: ");
                    fflush(stdout);
                            
            }
                    
        }
        else
        {
            usleep(200000); // 200 ms bekleme
                    
        }
           
    }
        return NULL;
}

// monitor thread bu threadin görevi her processin shared memory içinde statusu güncellemek
// her 2 saniyede processleri kontrol ediyor
void *monitor_thread_func(void *arg)
{
        (void)arg;

        while (g_monitor_running)
    {
        
        sleep(2); 

        // bu instancein child processleri için waitpid kullanacağız
        // başka instanceların processleri için kill(pid,0) ile kontrol edeceğiz

        pid_t my_child_pids[50];
        int my_count = 0;

        lock_shared(); // kontrol etmeden önce semaforu kapatıyoruz bir hata olmasın diye
        int pc = g_shared->process_count;

            for (int i = 0; i < pc && i < 50; i++){
                ProcessInfo *p = &g_shared->processes[i];
                // sadece aktif olan ve hala çalışan processleri kontrol ediyoruz
            if (!p->is_active) continue;
                if (p->status != RUNNING) continue;

                if (p->owner_pid == g_self_pid)
            {
                // bu instancein child processleri
                my_child_pids[my_count++] = p->pid;
                            
            }
            else{

                // Diğer instance'ların processleri için durum kontrolü
                if (kill(p->pid, 0) == -1 && errno == ESRCH)
                {
                    p->status = TERMINATED;
                    p->is_active = 0; // Temizle
                    printf("\n[MONITOR] Process %d sonlandı\n", p->pid);
                    printf("Seçiminiz: ");
                    fflush(stdout);

                    // diğer instaceleri bildir
                    send_ipc_message(CMD_STATUS_CHANGE, p->pid);
                                    
                }
            }           
        }
            unlock_shared();

            // bu instance'in childlarini waitpid ile kontrol
        for (int k = 0; k < my_count; k++)
        {
            int status;
            pid_t ret = waitpid(my_child_pids[k], &status, WNOHANG);
            
            if (ret > 0){
                lock_shared();
                int pc2 = g_shared->process_count;
                for (int i = 0; i < pc2 && i < 50; i++){
                    ProcessInfo *p = &g_shared->processes[i];
                    
                    if (p->is_active && p->pid == ret){
                        p->status = TERMINATED;
                        p->is_active = 0; // Temizle
                        break;
                    }
                                    
                }
                    unlock_shared();

                    printf("\n[MONITOR] Process %d sonlandı\n", ret);
                    printf("Seçiminiz: ");
                    fflush(stdout);

                    // diğer instancleri bildir
                send_ipc_message(CMD_STATUS_CHANGE, ret);
                            
            }
                    
        }
           
    }

        return NULL;
}

// Çıkışta attached processleri sonlandır
void terminate_attached_on_exit()
{
        pid_t to_kill[50];
        int count = 0;

        lock_shared();
        int pc = g_shared->process_count;

        for (int i = 0; i < pc && i < 50; i++)
    {
                ProcessInfo *p = &g_shared->processes[i];

                if (!p->is_active) continue;
                if (p->status != RUNNING) continue;

                if (p->owner_pid == g_self_pid && p->mode == ATTACHED){
                
                    if (count < 50){
                    to_kill[count++] = p->pid;
                    }
        }
           
    }
        unlock_shared();

        for (int i = 0; i < count; i++)
    {
                pid_t pid = to_kill[i];

                if (kill(pid, SIGTERM) != -1)
        {
            // diğer instancleri bildir
            send_ipc_message(CMD_TERMINATE, pid);
                   
        }
           
    }

        // Shared memoryde sonlandırılanları temizlemek
    lock_shared();
        int pc2 = g_shared->process_count;
        for (int k = 0; k < count; k++){
            pid_t pid = to_kill[k];
            
            for (int i = 0; i < pc2 && i < 50; i++){
                ProcessInfo *p = &g_shared->processes[i];
                if (p->is_active && p->pid == pid){
                    p->status = TERMINATED;
                    p->is_active = 0;
                    break;
            }
                    
        }
           
    }
        unlock_shared();
}

int main(void)
{
        int select;
        pid_t self_pid = getpid();
        g_self_pid = self_pid;

        MakeSharedMemory();
        MakeSemaphore();
        MakeMessageQueue();

        // receiver thread başlat 
    if (pthread_create(&g_receiver_tid, NULL, receiver_thread_func, NULL) != 0)
    {
                perror("pthread_create receiver hatasi");
                exit(1);
           
    }

        // monitor thread başlat
    if (pthread_create(&g_monitor_tid, NULL, monitor_thread_func, NULL) != 0)
    {
                perror("pthread_create hatasi");
                exit(1);
           
    }

        printf("Program başladı, benim PID = %d\n", self_pid);

        while (1)
    {
        print_menu();
        // kullanıcı rakamlar yerini harf yada krakter yazarsa sistemde bi sıkıntı çıkmasın diye
        // input bufferi temizliyorum
        if (scanf("%d", &select) != 1)
        {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) {}
            continue;
        }

        if (select == 0){
            printf("Çıkış yapıldı\n");
            break;
        }

        switch (select){
        case 1:
        {
        // scanf sonra \n kalıyor, fgets için temizliyoruz
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) {}

        // userden ilk önce commandı string olarak alıyoruz
            char line[256];
            printf("Çalıstirilacak komutu girin : ");
            if (fgets(line, sizeof(line), stdin) == NULL){
                break;
            }

                int mode_select = -1;
                printf("Processin modu seçin (Attached = 0, Detached = 1): ");
                if (scanf("%d", &mode_select) != 1 || (mode_select != 0 && mode_select != 1)){
                    printf("Hata: Geçersiz mod seçimi.\n");
                    int c2;
                    while ((c2 = getchar()) != '\n' && c2 != EOF) {}
                    break;

            }

                // execvp() stringi kabul etmiyor argv istiyor
                // bu yüzden parse command() kullanacağız
                // aldığımız stringي parçlara bölüp her parçayı argv içinde yerleştırıyor
                char parse_buf[256];
                strncpy(parse_buf, line, sizeof(parse_buf));
                parse_buf[sizeof(parse_buf) - 1] = '\0';

                char *argv[32];
                if (parse_command(parse_buf, argv, 32) == 0){
                    printf("Bos komut!\n");
                    break;
                                
            }

            pid_t pid = fork();
            // yeni process child process

            if (pid < 0){
                perror("fork hatasi");
                                
            }
            else if (pid == 0)
            {
                    // DETACHED ise terminalden kopar
                if (mode_select == 1)
                {
                    if (setsid() == -1){
                        perror("setsid hatasi");
                        exit(1);
                                                                
                    }
                                        
                }

                    // arguments desteklemek için execvp
                    execvp(argv[0], argv);

                    // execvp başarısız olursa buraya düşer
                    perror("exec hatasi");
                    exit(1);

            }
            else
            {
                lock_shared(); // semaforu kapat
                                    
                int index = -1;
                                    // Shared memoryde boş yer bul
                for (int i = 0; i < 50; i++){
                    if (!g_shared->processes[i].is_active){
                        index = i;
                        break;
                                                                
                    }
                                        
                }

                    // exec işlemi bittikten sonra processi shared memorde saklayacağız
                    if (index != -1)
                {
                    ProcessInfo *p = &g_shared->processes[index];
                    p->pid        = pid;
                    p->owner_pid  = self_pid;

                    // komutu sakla 
                    size_t ln = strlen(line);
                    if (ln > 0 && line[ln - 1] == '\n') line[ln - 1] = '\0';
                    strncpy(p->command, line, sizeof(p->command));
                    p->command[sizeof(p->command) - 1] = '\0';

                    p->mode       = (mode_select == 1) ? DETACHED : ATTACHED;
                    p->status     = RUNNING;
                    p->start_time = time(NULL);
                    p->is_active  = 1;
                                             
                    if (index >= g_shared->process_count){
                        g_shared->process_count = index + 1; // Update max index
                                                                
                    }

                    printf("[SUCCESS] Process başlatıldı: PID %d\n", pid);

                    // diğer instancleri bildir (Başlatma)
                    send_ipc_message(CMD_START, pid);
                                        
                }
                else
                {
                    printf("Process listesi dolu \n"); // shared memoryde yer yoksa hata mesaji bu
                                        
                }
                    unlock_shared(); // semاforu aç
                                
            }
                    break;
                       
        }

        case 2:
        {
                // programları listelemek için shraed memorye gidip sakladığı programların biligleri alıcağım
                // ekrana göstereceğım
                lock_shared();

                    int active_count = 0;
                for (int i = 0; i < g_shared->process_count; i++)
            {
                    if (g_shared->processes[i].is_active) active_count++;
                                
            }

                    if (active_count == 0)
            {
                    printf("Henüz hiç aktif program çaliştirilmadı.\n");
                                
            }
            else
            {
                printf("\nÇALIŞAN PROGRAMLAR\n");
                printf("------------------------------------------------------------------------------------------------------\n");
                printf("| %-6s | %-20s | %-8s | %-6s | %-10s | %-8s |\n", 
                        "PID", "Command", "Mode", "Owner", "Süre (sn)", "Durum");
                printf("------------------------------------------------------------------------------------------------------\n");

                    for (int i = 0; i < g_shared->process_count; i++)
                {
                        ProcessInfo *p = &g_shared->processes[i];
                        if (!p->is_active) continue;

                        time_t current_time = time(NULL);
                        long elapsed_time = (p->status == RUNNING) ? (long)(current_time - p->start_time) : (long)0;
                                             
                        printf("| %-6d | %-20s | %-8s | %-6d | %-10ld | %-8s |\n",
                        p->pid,                                    
                        p->command,                                
                        (p->mode == DETACHED ? "Detached" : "Attached"), 
                        p->owner_pid,                              
                        elapsed_time,                              
                        status_to_string(p->status));

                   
                }
                    printf("------------------------------------------------------------------------------------------------------\n");
                    printf("Toplam: %d process\n", active_count);
            }
                unlock_shared();
                break;
                       
        }

        case 3:
        {
           
            pid_t target;
            printf("Sonlandirilacak programin pid değerini girin: ");
            if (scanf("%d", &target) != 1){
                int ch;
                while ((ch = getchar()) != '\n' && ch != EOF) {}
                break;
                                
            }

            if (kill(target, SIGTERM) == -1){
                perror("kill hatasi (Process bulunamadı/yetki yok)");
            }
            else
            {
                printf("[INFO] Process %d'e SIGTERM sinyali gönderildi\n", target);

                    // Diğer instaceleri bildir (Sonlandırma)
                    send_ipc_message(CMD_TERMINATE, target);
                                
            }

                break;
                       
        }

        default :
            printf("geçersiz seçim lütfen doğru seçim giriniz.\n");
                   
        }
           
    }

        // Çıkışta attached modda çalışan ve bu instance tarafından başlatılan tüm processler sonlandırılmalı
    terminate_attached_on_exit();

        // receiver thread kapatmak 
    g_receiver_running = 0;
        pthread_join(g_receiver_tid, NULL);

        // monitor thread kapatmak
    g_monitor_running = 0;
        pthread_join(g_monitor_tid, NULL);

        // bütün işlemler bittikten sonra shared memor semاfor ve msg queue temizle
    cleanup_semaphore();
        cleanup_shared_memory();
        cleanup_message_queue();
        return 0;
}
