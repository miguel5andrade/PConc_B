#include "gd.h"
#include "image-lib.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* the directories wher output files will be placed */
#define RESIZE_DIR "Resize-dir/"
#define THUMB_DIR "Thumbnail-dir/"
#define WATER_DIR "Watermark-dir/"
#define LIST_NAME "image-list.txt"

/* array containg the names of files to be processed */
char **files;

/* input watermark images */
gdImagePtr watermark_img;

char *DIR_NAME;

// Declaração dos 3 pipes que vamos precisar
int pipe_main_water[2];
int pipe_water_resize[2];
int pipe_water_thumbnail[2];
int pipe_destroy_resize[2];


// estrutura com informação para enviar nos pipes
typedef struct _info_pipe
{
    int image_index;
    gdImagePtr image_watermark;

} info_pipe;

/*******************************************************
 * exists()
 *
 * arguments: const char *fname: name of the directory
 *
 * return: 1 if exists
 * 		   0 if does not exists
 *
 *****************************************************/
int exists(const char *fname)
{
    FILE *file;
    if ((file = fopen(fname, "r")))
    {
        fclose(file);
        return 1;
    }
    return 0;
}

// função que vai aplicar a todas as imagens a marca de agua e guarda na watermark-dir
void *Processa_watermarks(void *arg)
{
    /* input images */
    gdImagePtr in_img;


    /* file name of the image created and to be saved on disk	 */
    char out_file_name[100];
    char buffer[100];
    int image_index;

    //dois pipes que vao passar a informação para o resize e a thumbnail
    info_pipe *send_to_resize = (info_pipe *)calloc(1, sizeof(info_pipe));


    while (1)
    {
        // vai buscar ao pipe os index
        read(pipe_main_water[0], &image_index, sizeof(image_index));

        if (image_index == -1)
        {
            // se encontrarmos um -1 acabamos a thread e passamos o -1 para a próxima
            send_to_resize->image_index = -1;
            send_to_resize->image_watermark = NULL;

            write(pipe_water_resize[1], send_to_resize, sizeof(info_pipe)); 
            write(pipe_water_thumbnail[1], send_to_resize, sizeof(info_pipe)); 
            free(send_to_resize);

            pthread_exit(NULL);
        }

        sprintf(out_file_name, "%s%s%s", DIR_NAME, WATER_DIR, files[image_index]);
        // verificar se esta imagem já foi processada.
        if (access(out_file_name, F_OK) != -1)
        {
            // vamos buscar a imagem que ja existe e passamos no pipe.
            send_to_resize->image_watermark = read_png_file(out_file_name);
            send_to_resize->image_index = image_index;

            write(pipe_water_resize[1], send_to_resize, sizeof(info_pipe)); 
            write(pipe_water_thumbnail[1], send_to_resize, sizeof(info_pipe)); 
            continue;
        }

        /* load of the input file */
        strcpy(buffer, DIR_NAME);
        strcat(buffer, files[image_index]);
        in_img = read_png_file(buffer);
        if (in_img == NULL)
        {
            continue;
        }

        /* add watermark */
        send_to_resize->image_watermark = add_watermark(in_img, watermark_img);
        if (send_to_resize->image_watermark == NULL)
        {
            fprintf(stderr, "Impossible to creat thumbnail of %s image\n", files[image_index]);
            continue;
        }
        else
        {
            /* save watermark */
            if (write_png_file(send_to_resize->image_watermark, out_file_name) == 0)
            {
                fprintf(stderr, "Impossible to write %s image\n", out_file_name);
            }
        }
        gdImageDestroy(in_img);

        send_to_resize->image_index = image_index;

        write(pipe_water_resize[1], send_to_resize, sizeof(info_pipe));
        write(pipe_water_thumbnail[1], send_to_resize, sizeof(info_pipe));
    }
}

// Função que vai redimensionar todas as imagens
void *Processa_resizes(void *arg)
{

    /* output images */
    gdImagePtr out_resized_img;
    /* file name of the image created and to be saved on disk	 */
    char out_file_name[100];
    int image_index;
    info_pipe *receive_from_pipe = (info_pipe *)calloc(1, sizeof(info_pipe));

    while (1)
    {
        // vai buscar ao pipe os index e a imagem com a watermark aplicada
        read(pipe_water_resize[0], receive_from_pipe, sizeof(info_pipe));
        image_index = receive_from_pipe->image_index;

        if (image_index == -1)
        {   
            receive_from_pipe->image_watermark = NULL;
            write(pipe_destroy_resize[1], receive_from_pipe, sizeof(info_pipe));
            free(receive_from_pipe);
            pthread_exit(NULL);
        }

        sprintf(out_file_name, "%s%s%s", DIR_NAME, RESIZE_DIR, files[image_index]);
        if (access(out_file_name, F_OK) != -1)
        {
            write(pipe_destroy_resize[1], receive_from_pipe, sizeof(info_pipe));
            continue;
        }

        out_resized_img = resize_image(receive_from_pipe->image_watermark, 800);
        if (out_resized_img == NULL)
        {
            fprintf(stderr, "Impossible to resize %s image\n", files[image_index]);
        }
        else
        {
            /* save resized */

            if (write_png_file(out_resized_img, out_file_name) == 0)
            {
                fprintf(stderr, "Impossible to write %s image\n", out_file_name);
            }
            gdImageDestroy(out_resized_img);
        }

        write(pipe_destroy_resize[1], receive_from_pipe, sizeof(info_pipe));
    }
}

// Função que criar a thumbnail para todas as imagens
void *Processa_Thumbnails(void *arg)
{
    /* output images */
    gdImagePtr out_thumb_img;
    /* file name of the image created and to be saved on disk	 */
    char out_file_name[100];
    int image_index;
    info_pipe *receive_from_pipe = (info_pipe *)calloc(1, sizeof(info_pipe));

    while (1)
    {
        // vai buscar ao pipe os index e a imagem com a watermark aplicada
        read(pipe_water_thumbnail[0], receive_from_pipe, sizeof(info_pipe));
        image_index = receive_from_pipe->image_index;

        if (image_index == -1)
        {
            free(receive_from_pipe);
            pthread_exit(NULL);
        }

        sprintf(out_file_name, "%s%s%s", DIR_NAME, THUMB_DIR, files[image_index]);
        if (access(out_file_name, F_OK) != -1)
        {
            continue;
        }

        out_thumb_img = make_thumb(receive_from_pipe->image_watermark, 150);
        if (out_thumb_img == NULL)
        {
            fprintf(stderr, "Impossible to creat thumbnail of %s image\n", files[image_index]);
        }
        else
        {
            /* save thumbnail image */
            if (write_png_file(out_thumb_img, out_file_name) == 0)
            {
                fprintf(stderr, "Impossible to write %s image\n", out_file_name);
            }
            gdImageDestroy(out_thumb_img);
        }
    }
}

/****************************************************************************************************************
 * verification()
 *
 * arguments: char ***files: variavel onde vai ser guardado os nomes dos ficheiros que precisamos de processar
 * 			  char dir_name: nome da diretoria que contem as imagens a processar
 *
 * return: retorna um vetor de dois inteiros em que index = 0 -> numero de ficheiros no ficheiro de texto
 * 													index = 1 -> numero de ficheiros a ser processados
 *
 * side efects: aloca memoria para char ***files
 ***************************************************************************************************************/
char **verification(int *number_of_files)
{
    FILE *text_file;
    char buffer[200], **files, *file_name;
    int tamanho, i = 0;
    *number_of_files = 0;

    file_name = (char *)malloc(sizeof(char) * (strlen(DIR_NAME) + strlen(LIST_NAME) + 1));
    if (file_name == NULL)
        exit(0);

    strcpy(file_name, DIR_NAME);
    strcat(file_name, LIST_NAME);

    text_file = fopen(file_name, "r");
    if (text_file == NULL)
    {
        exit(2);
    }

    // count how many files there is
    while (fscanf(text_file, "%s", buffer) == 1)
    {
        (*number_of_files)++;
    }

    files = (char **)malloc(sizeof(char *) * (*number_of_files));
    if (files == NULL)
        exit(0);

    fseek(text_file, 0, SEEK_SET);

    while (fscanf(text_file, "%s", buffer) == 1)
    {
        tamanho = strlen(buffer);

        // alocation of number_of_files
        files[i] = (char *)malloc(sizeof(char) * (tamanho + 1));
        strcpy(files[i], buffer);
        i++;
    }

    fclose(text_file);
    free(file_name);

    return files;
}

int main(int argc, char *argv[])
{
    /* length of the files array (number of files to be processed)	 */
    int n_files = 0;
    int n_threads = 0;
    char *NEW_RESIZE_DIR, *NEW_THUMB_DIR, *NEW_WATER_DIR;

    pthread_t thread_id;
    pthread_t *ids; // vetor onde vamos guardar os ids de cada thread

    //vamos usar 3 pipes, um para cada processo
    if(pipe(pipe_main_water) != 0){
        printf("Error creating pipe.\n");
        exit(-1);
    }
    if(pipe(pipe_water_resize) != 0){
        printf("Error creating pipe.\n");
        exit(-1);
    }
    if(pipe(pipe_water_thumbnail) != 0){
        printf("Error creating pipe.\n");
        exit(-1);
    }
    if(pipe(pipe_destroy_resize) != 0){
        printf("Error creating pipe.\n");
        exit(-1);
    }
    




    /* check if the program has all arguments */
    if (argc != 3)
    {
        printf("To many or to few arguments\n");
        exit(1);
    }

    // segundo argumento é o número de threads.
    n_threads = atoi(argv[2]);

    // vetor onde vamos guardar os ids das threads, n_threads*3 pq cada processo vai criar o número de threads indicadas.
    ids = (pthread_t *)calloc(n_threads * 3, sizeof(pthread_t));
    if (ids == NULL)
        exit(0);

    // check if directory passs in argv[1] has '/' in the end
    if (argv[1][strlen(argv[1])] == '/')
    {
        DIR_NAME = (char *)malloc(sizeof(char) * (strlen(argv[1]) + 1));
        sprintf(DIR_NAME, "%s", argv[1]);
        DIR_NAME[strlen(argv[1])] = '\0';
    }
    else
    {
        // if it doesnt we put it in
        DIR_NAME = (char *)malloc(sizeof(char) * (strlen(argv[1]) + 2));
        sprintf(DIR_NAME, "%s/", argv[1]);
        DIR_NAME[strlen(argv[1]) + 1] = '\0';
    }

    // chech if directory passs in argv[1] exists
    if (exists(DIR_NAME) == 0)
    {
        printf("Directory not found\n");
        exit(3);
    }

    /* selecting the files to be processed*/
    files = verification(&n_files);

    /*Creation of new directories*/
    NEW_RESIZE_DIR = (char *)malloc(sizeof(char) * (strlen(DIR_NAME) + strlen(RESIZE_DIR) + 1));
    if (NEW_RESIZE_DIR == NULL)
        exit(0);

    sprintf(NEW_RESIZE_DIR, "%s%s", DIR_NAME, RESIZE_DIR);

    if (create_directory(NEW_RESIZE_DIR) == 0)
    {
        fprintf(stderr, "Impossible to create %s directory\n", RESIZE_DIR);
        exit(-1);
    }

    NEW_THUMB_DIR = (char *)malloc(sizeof(char) * (strlen(DIR_NAME) + strlen(THUMB_DIR) + 1));
    if (NEW_THUMB_DIR == NULL)
        exit(0);

    sprintf(NEW_THUMB_DIR, "%s%s", DIR_NAME, THUMB_DIR);

    if (create_directory(NEW_THUMB_DIR) == 0)
    {
        fprintf(stderr, "Impossible to create %s directory\n", THUMB_DIR);
        exit(-1);
    }

    NEW_WATER_DIR = (char *)malloc(sizeof(char) * (strlen(DIR_NAME) + strlen(WATER_DIR) + 1));
    if (NEW_WATER_DIR == NULL)
        exit(0);

    sprintf(NEW_WATER_DIR, "%s%s", DIR_NAME, WATER_DIR);
    if (create_directory(NEW_WATER_DIR) == 0)
    {
        fprintf(stderr, "Impossible to create %s directory\n", WATER_DIR);
        exit(-1);
    }
    watermark_img = read_png_file("watermark.png");
    if (watermark_img == NULL)
    {
        fprintf(stderr, "Impossible to read %s image\n", "watermark.png");
        exit(-1);
    }

   
    for (int i = 0; i < n_files; i++)
    {
        // injetar no pipe do main para a watermark os index das imagens na tabela
        write(pipe_main_water[1], &i, sizeof(i));
    }

    // para as threads saberem que acabaram as imagens para processar podemos injetar nos pipes -1 tantos quanto o n de threads
    int flag_fim = -1;
    for (int j = 0; j < n_threads; j++)
    {
        write(pipe_main_water[1], &flag_fim, sizeof(flag_fim));
    }

    int count = 0;
    // criar as threads do watermark
    for (int i = 0; i < n_threads; i++)
    {
        pthread_create(&thread_id, NULL, Processa_watermarks, NULL);
        ids[count] = thread_id;
        count++;
    }

    // criar threads do resize
    for (int k = 0; k < n_threads; k++)
    {
        pthread_create(&thread_id, NULL, Processa_resizes, NULL);
        ids[count] = thread_id;
        count++;
    }

    // criar threads thumbnail
    for (int r = 0; r < n_threads; r++)
    {
        pthread_create(&thread_id, NULL, Processa_Thumbnails, NULL);
        ids[count] = thread_id;
        count++;
    }

    // dar join de todas as threads
    for (int j = 0; j < 3 * n_threads; j++)
    {
        thread_id = ids[j];
        pthread_join(thread_id, NULL);
    }


    info_pipe *receive_destroy_resize = (info_pipe *)calloc(1, sizeof(info_pipe));

    while(1){

        read(pipe_destroy_resize[0], receive_destroy_resize, sizeof(info_pipe));

        if(receive_destroy_resize->image_index == -1){
            break;
        }

        gdImageDestroy(receive_destroy_resize->image_watermark);
    }

    free(receive_destroy_resize);


    // Free file names
    for (int i = 0; i < n_files; i++)
    {
        free(files[i]);
    }
    free(files);

    gdImageDestroy(watermark_img);
    free(ids);
    free(DIR_NAME);
    free(NEW_RESIZE_DIR);
    free(NEW_THUMB_DIR);
    free(NEW_WATER_DIR);

    return (EXIT_SUCCESS);
}