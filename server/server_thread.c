//#define _XOPEN_SOURCE 700   /* So as to allow use of `fdopen` and `getline`.  */

#include "common.h"
#include "server_thread.h"

#include <netinet/in.h>
#include <netdb.h>

#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>

#include <time.h>

enum { NUL = '\0' };

enum {
    /* Configuration constants.  */
            max_wait_time = 30,
    server_backlog_size = 5
};

unsigned int server_socket_fd;

// Nombre de client enregistré.
int nb_registered_clients;

// Variable du journal.
// Nombre de requêtes acceptées immédiatement (ACK envoyé en réponse à REQ).
unsigned int count_accepted = 0;

// Nombre de requêtes acceptées après un délai (ACK après REQ, mais retardé).
unsigned int count_wait = 0;

// Nombre de requête erronées (ERR envoyé en réponse à REQ).
unsigned int count_invalid = 0;

// Nombre de clients qui se sont terminés correctement
// (ACK envoyé en réponse à CLO).
unsigned int count_dispatched = 0;

// Nombre total de requête (REQ) traités.
unsigned int request_processed = 0;

// Nombre de clients ayant envoyé le message CLO.
unsigned int clients_ended = 0;

// TODO: Ajouter vos structures de données partagées, ici.
int nombre_ressources;
int nombre_clients;

/*
 * n  = number of threads
 * m = number of ressources
 * available = vector of length m
 * max = [n][m] = process n can request at most k instances
 *                of ressource m
 * allocation = [n][m] = process n has k instaces of
 *                the ressouce m
 * need = [n][m] = process n may need k instances of
 *                the ressource m. We need to calculate it
 *                need[i][j] = max[i][j] - allocation[i][j]
 *  (available + allocation = new_available)
 *
 * */
int *available;
int **allocation;
int **max;
int **need;

int *ressource;


// Mutex
pthread_mutex_t accepte_sans_delai;
pthread_mutex_t accepte_avec_delai;
pthread_mutex_t erreur_apres_requete;
pthread_mutex_t client_finis_correctement;
pthread_mutex_t requetes_proceses;

pthread_mutex_t max_modifie;


/********************************************************************************************************************/

void initialiser_tableaux (int nb_clients, int nb_ressources) {

  available = malloc(nb_ressources * sizeof(int));

  allocation = malloc(sizeof(int*) * nb_clients);
  max = malloc(sizeof(int*) * nb_clients);
  need = malloc(sizeof(int*) * nb_clients);

  for (int i = 0; i < nb_ressources; ++i) {
    allocation[i] = malloc(sizeof(int) * nb_ressources);
    max[i] = malloc(sizeof(int) * nb_ressources);
    need[i] = malloc(sizeof(int) * nb_ressources);
  }

}

void liberer_tableaux (int nb_ressources) {

  for (int i = 0; i < nb_ressources; ++i) {
    free(allocation[i]);
    free(max[i]);
    free(need[i]);
  }

  free(available);
  free(allocation);
  free(max);
  free(need);
}

int recevoir_ressource(int socket_fd){

  int ress[1]={ACK};
  int stat = 0;
  struct cmd_header_t header = { .nb_args=0 };

  int len = read_socket(socket_fd, &header, sizeof(header),max_wait_time * 1000);
  if (len > 0) {

    if(header.cmd==1){

      request_processed++;
      printf("**************************Ressources*************************\n");

      nombre_ressources = header.nb_args;
      nombre_clients = 5; // TODO Trouver le nombre de clients
      initialiser_tableaux(nombre_clients, nombre_ressources);

      for(int i=0; i<nombre_ressources; i++){
        read_socket(socket_fd,&available[i],sizeof(available[i]),max_wait_time*1000);
        //printf(" %d - ",available[i]);
      }
      //printf("\n");

      count_accepted++;

      send(socket_fd ,&ress, sizeof(ress) ,0);
    } else {
      printf("ERR\n");
      stat = -1;
    }
  } else {
    printf("Echec dans la configuration...\n");
    stat = -1;
  }



  close(socket_fd);
  return stat;
}

int recevoir_beg(int socket_fd){
  int rng=0;
  int stat = 0;
  printf("************************Initialisation du serveur********************\n\n");

  struct cmd_header_t header = { .nb_args = 0 };

  int len = read_socket(socket_fd, &header, sizeof(header), max_wait_time * 1000);

  if(len>0){

    if(header.cmd==BEGIN){
      request_processed++;
      int len2 = read_socket(socket_fd, &rng, sizeof(rng), max_wait_time * 1000);
      printf("BEGIN nb.args=%d  rng=%d\n",header.nb_args, rng);

      count_accepted++;

      int ack[3]={ACK,1, rng};
      send(socket_fd ,ack, sizeof(ack) ,0);
    } else {
      printf("ERR\n");
      stat = -1;
    }
  }else{
    printf("Echec dans le debut...\n");
    stat = -1;
  }
  close(socket_fd);
  return stat;

}

void gerer_ini(int socket_fd,int cmd,int nb_args){

  int x=-11;

  if(cmd==INIT && nb_args > 0){
    //printf("************************INI********************\n\n");
    //printf("INIT");
    // on ajoute les ressouces maximales pour le thread
    pthread_mutex_lock(&max_modifie);

    pthread_mutex_unlock(&max_modifie);

    for(int i=0; i<nb_args+1; ++i){
      int len = read_socket(socket_fd,&x,sizeof(x),max_wait_time*1000);

      //printf(" %d",x);

    }
    //printf("\n");
    //printf("Sending reponse de INIT\n");
    int ack[2] = {4, 0};
    send(socket_fd ,ack, sizeof(ack) ,0);
  } else if(cmd==REQ){

    for(int i=0; i<nb_args+1; ++i){
      int len=read_socket(socket_fd,&x,sizeof(x),max_wait_time*1000);

      //printf(" %d",x);

    }
    //printf("\n");
    int ack[2] = {4, 1};
    //printf("Sending ACK 0..\n");
    send(socket_fd ,ack, sizeof(ack) ,0);

  }
  else{
    printf("ERR\n");
    //Emvoyer erreur.
  }

}

int accepte_ct(){
  struct sockaddr_in thread_addr;
  socklen_t socket_len = sizeof (thread_addr);
  int thread_socket_fd = -1;
  while (thread_socket_fd < 0)
  {
    thread_socket_fd = accept (server_socket_fd, (struct sockaddr *) &thread_addr, &socket_len);
  }
  return thread_socket_fd;
}

/********************************************************************************************************************/

void
st_init ()
{
  // Initialise le nombre de clients connecté.
  nb_registered_clients = 0;

  // TODO

  // Attend la connection d'un client et initialise les structures pour
  // l'algorithme du banquier.
  int socket=accepte_ct();

  if (recevoir_beg(socket) < 0) {
    exit(1);
  }

  socket=accepte_ct();

  if (recevoir_ressource(socket) < 0) {
    exit(1);
  }

  // END TODO

}

void
st_process_requests (server_thread * st, int socket_fd)
{
  // TODO: Remplacer le contenu de cette fonction

  struct cmd_header_t header = { .nb_args = 0 };

  int len = read_socket(socket_fd, &header, sizeof(header), max_wait_time*1000);

  if (len > 0) {
    if (header.cmd != END) {
      if (len != sizeof(header.cmd) && len != sizeof(header)) {
        printf ("Thread %d received invalid command size=%d!\n", st->id, len);
      }

      printf("\nThread %d received command=%d, nb_args=%d\n", st->id, header.cmd, header.nb_args);
      //printf("header.nbargs = %d\n", header.nb_args);
      if (header.nb_args > 0) {
        gerer_ini(socket_fd,header.cmd,header.nb_args);
      }

    } else {
      // finir l'execution du thread serveur
    }

  } else {
    if (len == 0) {
      fprintf(stderr, "Thread %d, connection timeout\n", st->id);
    }
  }
}


void
st_signal ()
{
  // TODO: Remplacer le contenu de cette fonction


  liberer_tableaux(nombre_ressources);


  // TODO end
}

void *
st_code (void *param)
{
  server_thread *st = (server_thread *) param;

  struct sockaddr_in thread_addr;
  socklen_t socket_len = sizeof (thread_addr);
  int thread_socket_fd = -1;
  int end_time = time (NULL) + max_wait_time;

  // Boucle jusqu'à ce que `accept` reçoive la première connection.
  while (thread_socket_fd < 0)
  {
    thread_socket_fd =
            accept (server_socket_fd, (struct sockaddr *) &thread_addr,
                    &socket_len);

    if (time (NULL) >= end_time)
    {
      break;
    }
  }

  // Boucle de traitement des requêtes.
  while (accepting_connections)
  {
    if (!nb_registered_clients && time (NULL) >= end_time)
    {
      fprintf (stderr, "Time out on thread %d.\n", st->id);
      pthread_exit (NULL);
    }
    if (thread_socket_fd > 0)
    {
      st_process_requests (st, thread_socket_fd);
      close (thread_socket_fd);
      end_time = time (NULL) + max_wait_time;
    }
    thread_socket_fd =
            accept (server_socket_fd, (struct sockaddr *) &thread_addr,
                    &socket_len);
  }
  return NULL;
}


//
// Ouvre un socket pour le serveur.
//
void
st_open_socket (int port_number)
{
  server_socket_fd = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (server_socket_fd < 0)
    perror ("ERROR opening socket");

  if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0) {
    perror("setsockopt()");
    exit(1);
  }

  struct sockaddr_in serv_addr;
  memset (&serv_addr, 0, sizeof (serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons (port_number);

  if (bind
              (server_socket_fd, (struct sockaddr *) &serv_addr,
               sizeof (serv_addr)) < 0)
    perror ("ERROR on binding");

  listen (server_socket_fd, server_backlog_size);
}


//
// Affiche les données recueillies lors de l'exécution du
// serveur.
// La branche else ne doit PAS être modifiée.
//
void
st_print_results (FILE * fd, bool verbose)
{
  if (fd == NULL) fd = stdout;
  if (verbose)
  {
    fprintf (fd, "\n---- Résultat du serveur ----\n");
    fprintf (fd, "Requêtes acceptées: %d\n", count_accepted);
    fprintf (fd, "Requêtes : %d\n", count_wait);
    fprintf (fd, "Requêtes invalides: %d\n", count_invalid);
    fprintf (fd, "Clients : %d\n", count_dispatched);
    fprintf (fd, "Requêtes traitées: %d\n", request_processed);
  }
  else
  {
    fprintf (fd, "%d %d %d %d %d\n", count_accepted, count_wait,
             count_invalid, count_dispatched, request_processed);
  }
}