/* This is a simple TCP server that listens on port 1234 and provides lists
 * of files to clients, using a protocol defined in file_server.proto.
 *
 * It directly deserializes and serializes messages from network, minimizing
 * memory use.
 * 
 * For flexibility, this example is implemented using posix api.
 * In a real embedded system you would typically use some other kind of
 * a communication and filesystem layer.
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "common.h"
#include "api.h"

/* This callback function will be called during the encoding.
 * It will write out any number of FileInfo entries, without consuming unnecessary memory.
 * This is accomplished by fetching the filenames one at a time and encoding them
 * immediately.
 */
bool ListFilesResponse_callback(pb_istream_t *istream, pb_ostream_t *ostream, const pb_field_iter_t *field)
{
    PB_UNUSED(istream);
    if (ostream != NULL && field->tag == ListFilesResponse_file_tag)
    {
        DIR *dir = *(DIR **)field->pData;
        struct dirent *file;
        FileInfo fileinfo = {};

        while ((file = readdir(dir)) != NULL)
        {
            fileinfo.inode = file->d_ino;
            strncpy(fileinfo.name, file->d_name, sizeof(fileinfo.name));
            fileinfo.name[sizeof(fileinfo.name) - 1] = '\0';

            /* This encodes the header for the field, based on the constant info
            * from pb_field_t. */
            if (!pb_encode_tag_for_field(ostream, field))
                return false;

            /* This encodes the data for the field, based on our FileInfo structure. */
            if (!pb_encode_submessage(ostream, FileInfo_fields, &fileinfo))
                return false;
        }

        /* Because the main program uses pb_encode_delimited(), this callback will be
        * called twice. Rewind the directory for the next call. */
        rewinddir(dir);
    }

    return true;
}

void handle_getFiles(int connfd, const char *path)
{
    DIR *directory = NULL;

    /* open the requested directory. */

    directory = opendir(path);
    printf("Listing directory: %s\n", path);

    /* List the files in the directory and transmit the response to client */
    {
        ListFilesResponse response = {};

        if (directory == NULL)
        {
            perror("opendir");

            /* Directory was not found, transmit error status */
            response.path_error = true;
        }
        else
        {
            /* Directory was found, transmit filenames */
            response.file = directory;
        }

        size_t size;
        pb_get_encoded_size(&size, ListFilesResponse_fields, &response);

        pb_byte_t buffer[size];
        pb_ostream_t output = pb_ostream_from_buffer(buffer, sizeof(buffer));

        if (!pb_encode(&output, ListFilesResponse_fields, &response))
        {
            printf("Encoding failed: %s\n", PB_GET_ERROR(&output));
        }
        else
        {
            Message msg = {};

            msg.Type = MessageType_GET_FILES_OK;
            strcpy(msg.P1, path);
            msg.Data.size = size;
            memcpy(msg.Data.bytes, buffer, size);

            pb_ostream_t socket_output = pb_ostream_from_socket(connfd);

            if (!pb_encode_delimited(&socket_output, Message_fields, &msg))
            {
                printf("Encoding failed: %s\n", PB_GET_ERROR(&output));
            }
        }
    }

    if (directory != NULL)
        closedir(directory);
}

/* Handle one arriving client connection.
 * Clients are expected to send a ListFilesRequest, terminated by a '0'.
 * Server will respond with a ListFilesResponse message.
 */
void handle_connection(int connfd)
{
    /* Decode the message from the client and open the requested directory. */

    Message msg = {};
    pb_istream_t input = pb_istream_from_socket(connfd);

    if (!pb_decode_delimited(&input, Message_fields, &msg))
    {
        printf("Decode failed: %s\n", PB_GET_ERROR(&input));
        return;
    }

    printf("Message received: %s\n", getMessageTypeName(msg.Type));

    switch (msg.Type)
    {
    case MessageType_GET_FILES:
        handle_getFiles(connfd, msg.P1);
        break;

    case MessageType_DIGITAL_WRITE_HIGH:
        api_digital_write_high(msg.P3);
        break;

    case MessageType_DIGITAL_WRITE_LOW:
        break;

    default:
        break;
    }
}

int main(int argc, char **argv)
{
    int listenfd, connfd;
    struct sockaddr_in servaddr;
    int reuse = 1;

    /* Listen on localhost:1234 for TCP connections */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    servaddr.sin_port = htons(PORT);
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
    {
        perror("bind");
        return 1;
    }

    if (listen(listenfd, 5) != 0)
    {
        perror("listen");
        return 1;
    }

    for (;;)
    {
        /* Wait for a client */
        connfd = accept(listenfd, NULL, NULL);

        if (connfd < 0)
        {
            perror("accept");
            return 1;
        }

        printf("Got connection.\n");

        handle_connection(connfd);

        printf("Closing connection.\n");

        close(connfd);
    }

    return 0;
}
