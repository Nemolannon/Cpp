#include <iostream>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

class Semaphore
{
    const unsigned int base;    //Первоначальное значение счетчика-семафора
    unsigned int val;   // Текущее значение счетчика-семафора
    std::mutex my_lock;
    public:
    Semaphore(unsigned int value) : val(value), base(value) {}
    ~Semaphore(){}

    bool dec()// Если val уже 0, возвращает false. Иначе уменьшает значение val на 1 и возвращает true
    {
        std::lock_guard<std::mutex>lg(my_lock);
        if(val == 0) return false;
        --val;
        return true;
    }

    bool inc()//Если val уже достигло первоначального значения, возвращает false. Иначе увеличивает val на 1 и возвращает true
    {
        std::lock_guard<std::mutex>lg(my_lock);
        if(val == base) return false;
        ++val;
        return true;
    }
};

namespace shared
{
    Semaphore allowed_connections(2);//Допустимо всего два соединения одновременно
    std::mutex my_lock;
    //int connections = 0;    // счетчик активных соединений
    int user1 = -1; // Сокет 1го пользователя
    int user2 = -1; // Сокет 2го пользователя
    char wait_message[] = "Wait your friend...";
    char ok_message[] = "Connection establish.";
    char disconnect_message[] = "Your friend disconnected";
};

void connection_proc(int sockfd)
{
    using namespace shared;
    int *my, *recipient;// Ссылка на сокет получателя
    {
        std::lock_guard<std::mutex>lg(my_lock); // Вход в критическую секцию
        if(user1 == -1) // Если user1 еще не занят, значит мы первые
        {
            my = &user1;// Будем записываться как user1
            recipient = &user2;// Определяем user2 в качестве получателя
        }
        else// иначе все наоборот
        {
            my = &user2;
            recipient = &user1;
        }
	*my = sockfd;	// Собственно, записываемся
    }
    int rec;//Сюда сохраним сокет получателя данных
    while(true)
    {
        {
            std::lock_guard<std::mutex>lg(my_lock);
            rec = *recipient;// Проверяем, зарегистрировался ли получатель
        }
        if(rec != -1) break; // Если да, сохраняем его сокет и продолжаем
        send(sockfd, wait_message, sizeof(wait_message), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Иначе ждем секунду и проверяем снова
    }

    send(sockfd, ok_message, sizeof(ok_message), 0);
    
    char buf[4096];
    int bytes_read;

    while(true)
    {
        bytes_read = recv(sockfd, buf, 4096, 0);// Получаем данные от отправителя
        if(bytes_read <= 0) break;// Ошибка
        send(rec, buf, bytes_read, 0);// Отправляем получателю
    }
   
	send(sockfd, disconnect_message, sizeof(disconnect_message), 0);
        close(sockfd);    //Разрываем соединение
        allowed_connections.inc();  // Освобождаем ресурс
}

int main()
{
    int sock, listener;
    struct sockaddr_in addr;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if(listener < 0)
    {
        perror("socket");
        exit(1);
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3425);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        exit(2);
    }

    listen(listener, 2);
    
    while(true)
    {
	std::cout << "Take a resourse" << std::endl;
        while(!shared::allowed_connections.dec())  //Занимаем ресурс или ждем, пока он освободится
        {
            for(int i = 0; i < 99999; ++i);
        }
	std::cout << "Try to connect" << std::endl;
        sock = accept(listener, NULL, NULL);// Пробуем соединиться
        if(sock < 0)// В случае ошибки
        {
            std::cout << "accept" << std::endl;// Сообщаем
            //shared::allowed_connections.inc(); //Освобождаем ресурс
            //continue;// все сначала
            exit(3);
        }
	std::cout << "connected" << std::endl;
        // Если соединение установлено
        std::thread new_connection(connection_proc, sock);  // Создаем поток и передаем ему сокет
        new_connection.detach();    // Запускаем
    }
    
    return 0;
}
