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
int *available;


//int pthread_mutex_init(pthread_mutex_t *restrict mutex, const pthread_mutexattr_t *restrict attr);
//int pthread_mutex_lock (pthread_mutex_t *mutex);

// Mutex
pthread_mutex_t debut_fait;
pthread_mutex_t config_fait;

// verifier si la configuration initial est deja fait
bool begin_recu = false;
bool config_recu = false;



void process_config_request(int socket_st) {

  struct cmd_header_t header = { .nb_args = 0 };

  int len = read_socket(socket_st, &header, sizeof(header), max_wait_time * 1000);

  int rng;
  if (header.nb_args == 1) {
    read_socket(socket_st, &rng, sizeof(rng), max_wait_time * 1000);
  } else { // Pour le reste de numeros il faut faire une boucle
    for (int i = 0; i < header.nb_args ; ++i) {
      // Inserer les arguments dans un autre tableau

    }
  }

  if (len > 0) {
    if (len != sizeof(header.cmd) && len != sizeof(header)) {
      printf ("Thread received invalid command size=%d!\n", len);
    } else {
      if (header.cmd == 0) {
        // Error checking
        if (begin_recu) {
          printf("Erreur. Deuxieme BEGIN pas possible");
          int deuxieme_begin[3] = {8, 1, 1};
          send(socket_st, deuxieme_begin, sizeof(deuxieme_begin), 0);
          return;
        }
        printf("Debut du serveur... BEGIN\n");
        int reponse[3] = {4, 1, rng};
        send(socket_st, reponse, sizeof(reponse), 0 );
        printf("Envoi de rng = %d\n", rng);
        begin_recu = true;
        return;
      } else if (header.cmd == 1) {
        int ack[2] = {4, 1};
        printf("Configuration du serveur...\n");
        send(socket_st, ack, sizeof(ack), 0);
        config_recu = true;
        return;
      } else {
        printf("\n\nElse --->  header.cmd = %d\n\n\n", header.cmd);
        printf("Erreur!!\n");
      }
      // dispatch of cmd void thunk(int sockfd, struct cmd_header* header);
    }
  } else {
    if (len == 0) {
      fprintf(stderr, "Thread connection timeout\n");
    }
  }
//
}

void
st_init ()
{

  // Initialise le nombre de clients connecté.
  nb_registered_clients = 0;

  // TODO

  // Attend la connection d'un client et initialise les structures pour
  // l'algorithme du banquier.

  struct sockaddr_in thread_addr;
  socklen_t socket_len = sizeof (thread_addr);
  int thread_socket_fd = -1;

  while (thread_socket_fd < 0)
  {
    thread_socket_fd = accept (server_socket_fd, (struct sockaddr *) &thread_addr, &socket_len);
  }

  // On fait la configuration initial avec le premier thread
  pthread_mutex_lock(&debut_fait);
  if (!begin_recu) {
    process_config_request(thread_socket_fd);
  }
  pthread_mutex_unlock(&debut_fait);

  printf("Begin fait!\n");



  //Il faut lire dans un boucle jusqu'a recevoir les données du client
  pthread_mutex_lock(&config_fait);
  if (!config_recu) {
    process_config_request(thread_socket_fd);
    printf("CONFIG");
  }
  pthread_mutex_unlock(&config_fait);

  printf("Config fait!");


  // END TODO
}


void
st_process_requests (server_thread * st, int socket_fd)
{
  // TODO: Remplacer le contenu de cette fonction
  struct pollfd fds[1];
  fds->fd = socket_fd;
  fds->events = POLLIN;
  fds->revents = 0;


  struct cmd_header_t header = { .nb_args = 0 };

  int len = read_socket(socket_fd, &header, sizeof(header), max_wait_time * 1000);

  if (len > 0) {
    if (len != sizeof(header.cmd) && len != sizeof(header)) {
      //printf ("Thread %d received invalid command size=%d!\n", st->id, len);
    } else {
      //printf("Thread %d received command=%d, nb_args=%d\n", st->id, header.cmd, header.nb_args);
      switch (header.cmd) {
        case 0:
          //printf("Value received is %d\n", header.cmd);

          break;
        case 1: printf("CONFIGURATION!\n"); break;
        case 2: printf("INITIALIZATION\n"); break;
        case 3: printf("REQUETE\n"); break;
        case 6: printf("END\n"); break;
        case 7: printf("CLOSE\n"); break;
        default: printf("Erreur!!\n"); break;
      }
      // dispatch of cmd void thunk(int sockfd, struct cmd_header* header);
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
  // On doit impĺementer END ici


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

  if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEPORT, &(int){ 1 }, sizeof(int)) < 0) {
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