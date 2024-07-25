#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdlib.h>

#define DEFAULT_FILE "index.html"

int global_port = 80;
int global_max_cons = 20;
char global_path[256] = ".";
GtkApplication *main_app;
bool stop_server = false;
GThread *server_thread = NULL;

typedef struct {
    GtkWidget *lbl_status;
    GtkWidget *btn_start;
    GtkWidget *btn_stop;
    GtkEntryBuffer *entry_buffer;
    GtkEntryBuffer *entry_buffer_max_cons;
    GtkEntryBuffer *entry_buffer_path;
} WidgetData;

void show_error_dialog(GtkApplication *app, const char *message) {
    GtkWidget *dialog;      

    dialog = gtk_message_dialog_new(NULL,
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_CLOSE,
                                    "%s", message);

    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(gtk_application_get_active_window(app)));

    g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);

    gtk_window_present(GTK_WINDOW(dialog));
}

void err_exit(char *message)
{
    perror(message);       
    exit(EXIT_FAILURE);
}

int get_line(int sock_fd, char *buffer, int length)
{
    int i = 0;

    while((i < length - 1) && (recv(sock_fd, &(buffer[i]), 1, 0) == 1))
    {
        if(buffer[i] == '\n')
            break;
        else
            i++;
    }
    if((i > 0) && (buffer[i - 1] == '\r'))
        i--;
    buffer[i] = '\0';
    return i;
}

int is_html(char *filename)
{
    if(strcmp(&(filename[strlen(filename) - 5]), ".html") == 0)
        return 1;
    if(strcmp(&(filename[strlen(filename) - 4]), ".htm") == 0)
        return 1;
    return 0;
}

size_t file_size(char *filename)
{
    struct stat file_info;

    if(stat(filename, &file_info) == -1)
        return 0;
    return file_info.st_size;
}

void http_service(int client_fd)
{
    char buffer[256], cmd[8], url[128], *filename;
    int length;
    FILE *stream;

    if(get_line(client_fd, buffer, 256) == 0)
        return;
    if(sscanf(buffer, "%7s %127s", cmd, url) < 2)
        return;
    while(get_line(client_fd, buffer, 256) > 0)
    {
        if((strcmp(cmd, "GET") != 0) && (strcmp(cmd, "HEAD") != 0))
            return;
    }

    filename = &(url[1]);
    if(strlen(filename) == 0)
        filename = DEFAULT_FILE;
    
    if((stream = fopen(filename, "r")) == NULL)
    {
        send(client_fd, "HTTP/1.0 404 Not Found\r\n"
                        "Content-type: text/html\r\n"
                        "Content-length: 117\r\n\r\n"
                        "<html><head><title>Error</title></head>"
                        "<body><hr><h2>index.html not found !</h2>Tiny-Webserver-GTK4<hr>"
                        "</body></html>", 200, 0);
        return;
    }

    send(client_fd, "HTTP/1.0 200 OK\r\n", 17, 0);
    if(is_html(filename))
        send(client_fd, "Content-type: text/html\r\n", 25, 0);
    sprintf(buffer, "Content-length: %d\r\n\r\n", (int)file_size(filename));
    send(client_fd, buffer, strlen(buffer), 0);
    if(strcmp(cmd, "GET") == 0)
    {
        while(!feof(stream))
        {
            length = fread(buffer, 1, 256, stream);
            if(length > 0)
            {
                send(client_fd, buffer, length, 0);
                printf("Sent %d bytes\n", length);
            }                
        }
    }
    fclose(stream);
    return;
}

gpointer start_server(gpointer user_data)
{
    WidgetData *data = (WidgetData *)user_data;
    
    int port_value = atoi(gtk_entry_buffer_get_text(data->entry_buffer));
    printf("webserver: Start server on port %d max. %d connections. Path to index.html:%s\n", port_value, global_max_cons, global_path);
    int sock_fd, client_fd, err, addr_size;
    struct sockaddr_in my_addr, client_addr;
    fd_set read_fds;
    struct timeval tv;

    sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    if(sock_fd == -1)
        err_exit("webserver: Can't create new socket\n");

    int opt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port_value);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    err = bind(sock_fd, (struct sockaddr*)&my_addr, sizeof(struct sockaddr_in));
    if(err == -1)
        err_exit("webserver: Can't bind name to socket\n");

    setuid(getuid());

    err = listen(sock_fd, global_max_cons);
    if(err == -1)
        err_exit("webserver: Can't listen on socket\n");
    
    signal(SIGCHLD, SIG_IGN);    

    while(!stop_server)
    {
        FD_ZERO(&read_fds);
        FD_SET(sock_fd, &read_fds);

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int select_result = select(sock_fd + 1, &read_fds, NULL, NULL, &tv);

        if (select_result > 0) {
            addr_size = sizeof(struct sockaddr_in);
            client_fd = accept(sock_fd, (struct sockaddr*)&client_addr, &addr_size);
            if(client_fd == -1) {
                perror("webserver: Can't accept client connection");
                continue;
            }

            pid_t pid = fork();
            if(pid == -1) {
                fprintf(stderr, "webserver: Can't fork child process\n");
                close(client_fd);
            } else if(pid == 0) {
                close(sock_fd);
                http_service(client_fd);
                close(client_fd);
                exit(EXIT_SUCCESS);
            } else {
                close(client_fd);
            }
        } else if (select_result < 0) {
            perror("select error");
            break;
        }
    }

    close(sock_fd);
    g_print("Server stopped\n");
    return NULL;
}

static void btn_start_clicked(GtkWidget *widget, gpointer user_data) {    
    GError *error = NULL;
    WidgetData *data = (WidgetData *)user_data;
    
    strcpy(global_path, gtk_entry_buffer_get_text(data->entry_buffer_path));    
    global_port = atoi(gtk_entry_buffer_get_text(data->entry_buffer));
    global_max_cons = atoi(gtk_entry_buffer_get_text(data->entry_buffer_max_cons));
    if(chdir(global_path) != 0)
    {
        show_error_dialog(main_app, "Can't set HTML path");
        return;
    }
    
    gtk_label_set_label(GTK_LABEL(data->lbl_status), "On");
    gtk_widget_set_sensitive(data->btn_start, FALSE);
    gtk_widget_set_sensitive(data->btn_stop, TRUE);
    
    stop_server = false;
    server_thread = g_thread_try_new("webserver", start_server, data, &error);
    if (!server_thread) {
        show_error_dialog(main_app, "Failed to create thread");
        g_printerr("Failed to create thread: %s\n", error->message);
        g_error_free(error);
        return;
    }
}

static void btn_stop_clicked(GtkWidget *widget, gpointer user_data) {
    WidgetData *data = (WidgetData *)user_data;
    
    strcpy(global_path, gtk_entry_buffer_get_text(data->entry_buffer_path));

    if (server_thread != NULL) {
        stop_server = true;
        g_thread_join(server_thread);
        server_thread = NULL;
    }

    gtk_label_set_label(GTK_LABEL(data->lbl_status), "Off");
    gtk_widget_set_sensitive(data->btn_start, TRUE);
    gtk_widget_set_sensitive(data->btn_stop, FALSE);
}

static void activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget *window, *lbl_status_title, *lbl_port_title, *lbl_max_cons_title, *entry_max_cons, *lbl_about;
    GtkWidget *lbl_status, *entry_port, *btn_start, *btn_stop, *grid, *center_box, *lbl_path, *entry_path;
    GtkEntryBuffer *buffer_port, *buffer_max_cons, *buffer_path;
    WidgetData *data = g_new(WidgetData, 1);

    window = gtk_application_window_new(app);
    
    gtk_window_set_title(GTK_WINDOW(window), "Tiny-Webserver-GTK4");
    gtk_window_set_default_size(GTK_WINDOW(window), 450, 300);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  
    grid = gtk_grid_new();
    center_box = gtk_center_box_new();
    lbl_status = gtk_label_new("Off");
    lbl_port_title = gtk_label_new("Port:");
    lbl_status_title = gtk_label_new("Status:");
    lbl_max_cons_title = gtk_label_new("Max connections:");
    lbl_path = gtk_label_new("Path to index.html:");
    lbl_about = gtk_label_new("Version: 1.2 - 2024 by Lennart Martens");
    entry_max_cons = gtk_entry_new();
    entry_port = gtk_entry_new();
    entry_path = gtk_entry_new();
    
    btn_start = gtk_button_new_with_label("Start");
    btn_stop = gtk_button_new_with_label("Stop");

    buffer_port = gtk_entry_buffer_new("80", -1);
    gtk_entry_set_buffer(GTK_ENTRY(entry_port), buffer_port);
    buffer_max_cons = gtk_entry_buffer_new("20", -1);
    gtk_entry_set_buffer(GTK_ENTRY(entry_max_cons), buffer_max_cons);
    buffer_path = gtk_entry_buffer_new(".", -1);
    gtk_entry_set_buffer(GTK_ENTRY(entry_path), buffer_path);

    data->lbl_status = lbl_status;
    data->btn_start = btn_start;
    data->btn_stop = btn_stop;
    data->entry_buffer = buffer_port;
    data->entry_buffer_max_cons = buffer_max_cons;
    data->entry_buffer_path = buffer_path;

    gtk_grid_attach(GTK_GRID(grid), lbl_status_title, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_port_title, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_status, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_port, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_path, 0, 2, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_path, 0, 3, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_start, 0, 4, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_stop, 0, 5, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_max_cons_title, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_max_cons, 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_about, 0, 6, 3, 1);

    gtk_widget_set_margin_top(lbl_status_title, 10);
    gtk_widget_set_margin_top(lbl_max_cons_title, 10);
    gtk_widget_set_margin_top(lbl_port_title, 10);
    gtk_widget_set_margin_top(lbl_path, 10);
    gtk_label_set_xalign(GTK_LABEL(lbl_about), 1.0);
    gtk_widget_set_margin_top(lbl_about, 20);
    gtk_entry_set_alignment(GTK_ENTRY(entry_port), 0.5);
    gtk_entry_set_alignment(GTK_ENTRY(entry_max_cons), 0.5);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

    gtk_window_set_child(GTK_WINDOW(window), center_box);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(center_box), grid);

    g_signal_connect(btn_start, "clicked", G_CALLBACK(btn_start_clicked), data);
    g_signal_connect(btn_stop, "clicked", G_CALLBACK(btn_stop_clicked), data);    

    gtk_widget_set_sensitive(btn_stop, FALSE);  // Initial state

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.gtk.example", G_APPLICATION_DEFAULT_FLAGS);
    main_app = app;
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}