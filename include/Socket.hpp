#ifndef JELFORD_SOCKET_HPP
#define JELFORD_SOCKET_HPP

#include <vector>       // Use as basic datatype in place of arrays
#include <exception>    // Because we have grown-up error handling

#include <functional>     // std::function

#include <sys/select.h>

#include <netinet/in.h>
#include <netdb.h>  // addrinfo

#include <string>

#include <memory>       // std::shared_ptr

namespace jelford 
{
    class Socket;
    
    class SocketException : public std::exception
    {   
        private:
            const int _errno;
            const jelford::Socket* m_socket;
            std::string where;
        public:
            SocketException(const int _errno, const Socket* socket, std::string where);
            
            virtual const char* what();
            const jelford::Socket* retrieve_socket() const;
    };

    class SocketTimeoutException: public SocketException
    {
        public:
            SocketTimeoutException(Socket*);

            virtual const char* what();
    };

    const char* address_get_error(int _errno);

    class AddressException : public std::exception
    {
        private:
            int _err;
            int _errno;
        public:
            AddressException(int err, int _errno);
            virtual const std::string msg() const;
    };


    const char* socket_get_error(int _errno);

    class Address
    {
        private:
            addrinfo* _addrinfo;
        public:
            Address(std::string host, std::string port, const addrinfo& hints); 
            Address(std::shared_ptr<sockaddr>, socklen_t, int, int);
            virtual ~Address();

            std::shared_ptr<sockaddr> address;
            socklen_t address_length;
            int protocol;
            int family;
            
            std::string family_string() const;
    };

    // Requires sys/socket.h
    class Socket
    {
        private:
            static const size_t __chunk_size = 256;
            int m_socket_descriptor;
            bool is_nonblocking;
            Socket(Socket&) = delete;
        public:
            Socket(int socket_family, int socket_type, int protocol) throw(SocketException);
            Socket(int file_descriptor, bool is_nonblocking);
            Socket(Socket&& other);
            Socket& operator=(Socket&& other);
            virtual ~Socket();
    
            int identify() const;
            bool is_listening() const;
            bool other_end_has_hung_up() const;

            void set_nonblocking(bool);
            void set_reuse(bool should_reuse);

            
            void listen(int backlogsize=32);
            Socket accept(sockaddr* addr, socklen_t* addrlen);

            void bind_to(Address& socket_address);
            void bind_to(sockaddr* socket_address, socklen_t socket_address_size);
            void bind_to(sockaddr_in& socket_address);
            void bind_to(sockaddr_in6& socket_address);

            void connect(Address& socket_address);
            void connect(sockaddr* socket_address, socklen_t socket_address_size);
            void connect(sockaddr_in& socket_address);
            void connect(sockaddr_in6& socket_address);
            
            std::vector<unsigned char> read(size_t length) const;
            std::vector<unsigned char> read() const;

            void write(const std::vector<unsigned char>&& data) const;
            void write(const std::vector<unsigned char>& data) const;
    };

    void wait_for_read(const Socket* socket);
    void wait_for_write(const Socket* socket);
    const std::vector<const Socket*> select_for_reading(std::vector<const Socket*>& sockets);
    const std::vector<const Socket*> select_for_writing(std::vector<const Socket*>& sockets);
    int _select_for_reading(int, fd_set&);
    int _select_for_writing(int, fd_set&);
    std::vector<const Socket*> _select_for(std::vector<const Socket*>& sockets, std::function<int(int, fd_set&)>);
}

#endif
