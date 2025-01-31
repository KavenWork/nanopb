/* This is a simple TCP client that connects to port 1234 and prints a list
 * of files in a given directory.
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

/* This callback function will be called once for each filename received
 * from the server. The filenames will be printed out immediately, so that
 * no memory has to be allocated for them.
 */
bool ListFilesResponse_callback(pb_istream_t *istream, pb_ostream_t *ostream, const pb_field_iter_t *field)
{
    PB_UNUSED(ostream);
    if (istream != NULL && field->tag == ListFilesResponse_file_tag)
    {
        FileInfo fileinfo = {};

        if (!pb_decode(istream, FileInfo_fields, &fileinfo))
            return false;

        printf("%-10lld %s\n", (long long)fileinfo.inode, fileinfo.name);
    }

    return true;
}

/* This function sends a request to socket 'fd' to list the files in
 * directory given in 'path'. The results received from server will
 * be printed to stdout.
 */
bool listdir(int fd, char *path)
{
    /* Construct and send the request to server */
    {
        Message request = {};
        request.Type = MessageType_GET_FILES;

        pb_ostream_t output = pb_ostream_from_socket(fd);

        /* In our protocol, path is optional. If it is not given,
         * the server will list the root directory. */
        if (path == NULL)
        {
            /* Default path */
            path = "/";
        }

        {
            if (strlen(path) + 1 > sizeof(request.P1))
            {
                fprintf(stderr, "Too long path.\n");
                return false;
            }

            strcpy(request.P1, path);
        }

        /* Encode the request. It is written to the socket immediately
         * through our custom stream. */
        if (!pb_encode_delimited(&output, Message_fields, &request))
        {
            fprintf(stderr, "Encoding failed: %s\n", PB_GET_ERROR(&output));
            return false;
        }
    }

    /* Read back the response from server */
    {
        Message msg = {};
        pb_istream_t input = pb_istream_from_socket(fd);

        if (!pb_decode_delimited(&input, Message_fields, &msg))
        {
            printf("Decode failed: %s\n", PB_GET_ERROR(&input));
            return false;
        }

        printf("Message received: %s\n", getMessageTypeName(msg.Type));

        switch (msg.Type)
        {
        case MessageType_GET_FILES_OK:
        {
            ListFilesResponse response = {};
            pb_istream_t input = pb_istream_from_buffer(msg.Data.bytes, msg.Data.size);

            if (!pb_decode(&input, ListFilesResponse_fields, &response))
            {
                fprintf(stderr, "Decode failed: %s\n", PB_GET_ERROR(&input));
                return false;
            }

            /* If the message from server decodes properly, but directory was
         * not found on server side, we get path_error == true. */
            if (response.path_error)
            {
                fprintf(stderr, "Server reported error.\n");
                return false;
            }
        }
        break;

        default:
            break;
        }
    }

    return true;
}

int main(int argc, char **argv)
{
    int sockfd;
    struct sockaddr_in servaddr;
    char *path = NULL;

    if (argc > 1)
        path = argv[1];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /* Connect to server running on localhost:1234 */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    servaddr.sin_port = htons(PORT);

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
    {
        perror("connect");
        return 1;
    }

    printf("Connected.\n");

    /* Send the directory listing request */
    if (!listdir(sockfd, path))
        return 2;

    /* Close connection */
    close(sockfd);

    return 0;
}
