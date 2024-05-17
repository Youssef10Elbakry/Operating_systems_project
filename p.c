#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <signal.h>

#define NUM_CHILDREN 5

GtkWidget *window;
GtkWidget *tree_view;
GtkWidget *buttons[NUM_CHILDREN];
GtkWidget *terminate_buttons[NUM_CHILDREN];
GtkWidget *cpu_label;
GtkWidget *memory_label;
GtkTreeStore *store;

typedef struct {
    pid_t pid;
    GtkTreeIter iter;
} ProcessInfo;

ProcessInfo main_children[NUM_CHILDREN] = {{0}};
int num_main_children_created = 0;
pid_t parent_pid;

void update_usage() {
    FILE *top_output = popen("top -bn1 | grep '%Cpu' | awk '{print $2}' && top -bn1 | grep 'KiB Mem' | awk '{print $5}'", "r");
    char cpu_usage[10], memory_usage[10];
    fscanf(top_output, "%s", cpu_usage);
    fscanf(top_output, "%s", memory_usage);
    pclose(top_output);

    gchar *cpu_text = g_strdup_printf("CPU Usage: %s%%", cpu_usage);
    gchar *memory_text = g_strdup_printf("Memory Usage: %s KB", memory_usage);
    gtk_label_set_text(GTK_LABEL(cpu_label), cpu_text);
    gtk_label_set_text(GTK_LABEL(memory_label), memory_text);
    g_free(cpu_text);
    g_free(memory_text);
}

void create_main_child_process(int index) {
    pid_t pid = fork();
    if (pid == 0) {
        printf("Main Child process %d: My PID is %d\n", index + 1, getpid());
        exit(0);
    } else if (pid < 0) {
        fprintf(stderr, "Error forking process\n");
        return;
    }

    char label[30];
    snprintf(label, sizeof(label), "Main Child %d (PID: %d)", index + 1, pid);
    gtk_tree_store_append(store, &main_children[index].iter, NULL);
    gtk_tree_store_set(store, &main_children[index].iter, 0, label, -1);

    main_children[index].pid = pid;
    num_main_children_created++;

    if (num_main_children_created >= NUM_CHILDREN) {
        for (int i = 0; i < NUM_CHILDREN; i++) {
            gtk_widget_set_sensitive(buttons[i], FALSE);
        }
    }

    update_usage();
}

void create_child_process(GtkTreeIter *parent_iter, int parent_index) {
    pid_t pid = fork();
    if (pid == 0) {
        printf("Child of Main Child %d: My PID is %d\n", parent_index + 1, getpid());
        exit(0);
    } else if (pid < 0) {
        fprintf(stderr, "Error forking process\n");
        return;
    }

    char label[50];
    snprintf(label, sizeof(label), "Child of Main Child %d (PID: %d)", parent_index + 1, pid);
    GtkTreeIter child_iter;
    gtk_tree_store_append(store, &child_iter, parent_iter);
    gtk_tree_store_set(store, &child_iter, 0, label, -1);

    update_usage();
}

void terminate_child_process(int index) {
    if (main_children[index].pid != 0) {
        GtkTreeIter iter;
        gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store), &iter, NULL, index);
        int num_children = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store), &iter);

        if (num_children == 0) {
            kill(main_children[index].pid, SIGTERM);
            printf("Child process %d (PID: %d) terminated\n", index + 1, main_children[index].pid);
            main_children[index].pid = 0;
            gtk_tree_store_remove(store, &main_children[index].iter);
        } else {
            printf("Main Child process %d has children, cannot terminate\n", index + 1);
        }

        update_usage();
    } else {
        printf("No child process to terminate for button %d\n", index + 1);
    }
}

void on_terminate_button_clicked(GtkWidget *button, gpointer data) {
    int index = GPOINTER_TO_INT(data);
    terminate_child_process(index);
}

void on_button_clicked(GtkWidget *button, gpointer data) {
    int index = GPOINTER_TO_INT(data);

    if (main_children[index].pid == 0) {
        create_main_child_process(index);
    } else {
        GtkTreeIter iter;
        gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store), &iter, NULL, index);
        create_child_process(&iter, index);
    }
}

void create_tree_view(GtkWidget *box) {
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;

    store = gtk_tree_store_new(1, G_TYPE_STRING);
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "Processes");
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", 0);

    gtk_box_pack_start(GTK_BOX(box), tree_view, TRUE, TRUE, 0);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Process Tree Visualization");
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), box);

    create_tree_view(box);

    // Add parent process to the tree view
    GtkTreeIter parent_iter;
    parent_pid = getpid();
    char parent_label[30];
    snprintf(parent_label, sizeof(parent_label), "Parent (PID: %d)", parent_pid);
    gtk_tree_store_append(store, &parent_iter, NULL);
    gtk_tree_store_set(store, &parent_iter, 0, parent_label, -1);

    cpu_label = gtk_label_new("CPU Usage: ");
    gtk_box_pack_end(GTK_BOX(box), cpu_label, FALSE, FALSE, 0);

    memory_label = gtk_label_new("Memory Usage: ");
    gtk_box_pack_end(GTK_BOX(box), memory_label, FALSE, FALSE, 0);

    for (int i = 0; i < NUM_CHILDREN; i++) {
        char label[20];
        snprintf(label, sizeof(label), "Create Child %d", i + 1);
        buttons[i] = gtk_button_new_with_label(label);
        g_signal_connect(buttons[i], "clicked", G_CALLBACK(on_button_clicked), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(box), buttons[i], FALSE, FALSE, 0);

        terminate_buttons[i] = gtk_button_new_with_label("Terminate");
        g_signal_connect(terminate_buttons[i], "clicked", G_CALLBACK(on_terminate_button_clicked), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(box), terminate_buttons[i], FALSE, FALSE, 0);
    }

    update_usage();

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}

