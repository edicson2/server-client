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


int **max;
int **allocation;
int **need;

int *available;
int *total_ressources;
int *total_allocation;

int count_ct = 0;


// Mutex
pthread_mutex_t accepte_sans_delai;
pthread_mutex_t accepte_avec_delai;
pthread_mutex_t erreur_apres_requete;
pthread_mutex_t client_finis_correctement;
pthread_mutex_t requetes_proceses;

pthread_mutex_t total_allocation_modifie;
pthread_mutex_t max_modifie;
pthread_mutex_t allocation_modifie;
pthread_mutex_t need_modifie;
pthread_mutex_t available_modifie;

pthread_mutex_t count_modifie;


/********************************************************************************************************************/

void initialiser_tableaux (int nb_clients, int nb_ressources) {

  total_ressources = malloc(sizeof(int) * nb_ressources);
  total_allocation = malloc(sizeof(int) * nb_ressources);
  max = malloc(sizeof(int*) * nb_clients);
  allocation = malloc(sizeof(int*) * nb_clients);
  need = malloc(sizeof(int*) * nb_clients);

  available = malloc(nb_ressources * sizeof(int));

  // on doit inclure l'identificateur du thread
  for (int i = 0; i < nb_ressources; ++i) {
    max[i] = malloc(sizeof(int) * nb_ressources);
    allocation[i] = malloc(sizeof(int) * nb_ressources);
    need[i] = malloc(sizeof(int) * nb_ressources);
  }

}

void liberer_tableaux (int nb_ressources) {

  for (int i = 0; i < nb_ressources; ++i) {
    free(max[i]);
    free(allocation[i]);
    free(need[i]);
  }

  free(available);

  free(total_ressources);
  free(total_allocation);
  free(max);
  free(allocation);
  free(need);
}

/*
 * Fonction qui recoit la configuration initial du serveur
 * et qui va ajouter le maximum des ressources disponibles
 * */

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

      // Ajouter le maximum des ressources
      for(int i=0; i<nombre_ressources; i++){
        int l = read_socket(socket_fd,&total_ressources[i],sizeof(total_ressources[i]),max_wait_time*1000);
        if (l < 0) {
          printf("Erreur dans la lecture de ressources\n");
          int err[3] = {ERR, 1, -1};
          send(socket_fd, err, sizeof(err), 0);
          exit(1);
        }
      }

      for (int j = 0; j < nombre_ressources; ++j) {
        total_allocation[j] = 0;
        available[j] = total_ressources[j];
      }

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


/*
 * Fonction qui recoit la premier commande pour le debut du serveur
 * */
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
      if (len2 > 0) {
        printf("BEGIN nb.args=%d  rng=%d\n",header.nb_args, rng);
        count_accepted++;
        int ack[3]={ACK,1, rng};
        send(socket_fd ,ack, sizeof(ack) ,0);
      } else {
        printf("Erreur dans RNG");
        int err[3] = {ERR, 1, -1};
        send(socket_fd, err, sizeof(err), 0);
      }
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

void calculer_need () {

  //pthread_mutex_lock(&need_modifie);
  for (int i = 0; i < count_ct; ++i) {
    for (int j = 0; j < nombre_ressources; ++j) {
      need[i][j] = max[i][j] - allocation[i][j];
    }
  }
  //pthread_mutex_unlock(&need_modifie);
}

void calculer_available () {

  pthread_mutex_lock(&available_modifie);
  for (int i = 0; i < nombre_ressources; ++i) {
    available[i] = total_ressources[i] - total_allocation[i];
  }
  pthread_mutex_unlock(&available_modifie);

}

void calculer_total_allocation () {
  int add = 0;
  pthread_mutex_lock(&total_allocation_modifie);
  for (int i = 0; i < nombre_ressources; ++i) {
    add = 0;
    for (int j = 0; j < count_ct; ++j) {
      add += allocation[j][i];
    }
    total_allocation[i] = add;
  }
  pthread_mutex_unlock(&total_allocation_modifie);

}

bool safe_state (int *safe_sequence) {

  int *work;
  work = malloc(nombre_ressources * sizeof(int));
  bool finish[count_ct];
  calculer_need();

  //pthread_mutex_lock(&available_modifie);
  for (int i = 0; i < nombre_ressources; ++i) {
    work[i] = available[i];
  }
  //pthread_mutex_unlock(&available_modifie);

  for (int j = 0; j < count_ct; ++j) {
    finish[j] = false;
  }

  int counter = 0;
  while (counter < count_ct) {
    bool found = false;
    for (int i = 0; i < count_ct ; ++i) {
      if (finish[i] == false) {
        int j;
        for (j = 0; j < nombre_ressources; ++j) {
          if (need[i][j] > work[j]) {
            break;
          }
        }
        if (j == nombre_ressources) {
          for (int k = 0; k < nombre_ressources; ++k) {
            work[k] += allocation[i][k];
          }
          safe_sequence[counter++] = i;
          finish[i] = true;
          found = true;
        }
      }
    }
    if (!found) {
      printf("UNSAFE!\n");
      return false;
    }
  }
  printf("Safe.\n");
  return true;
}

void remplir_donnees_initiales (int process_id, int ressource, int j) {
  pthread_mutex_lock(&max_modifie);
  max[process_id][j] = ressource;   // maximum de ressources par client
  pthread_mutex_unlock(&max_modifie);

  pthread_mutex_lock(&allocation_modifie);
  allocation[process_id][j] = 0;    // Rien encore alloue
  pthread_mutex_unlock(&allocation_modifie);

  pthread_mutex_lock(&need_modifie);
  need[process_id][j] = max[process_id][j]; // Au debut need == max
  pthread_mutex_unlock(&need_modifie);

  pthread_mutex_lock(&count_modifie);
  count_ct++;
  pthread_mutex_unlock(&count_modifie);
}

// TODO traiter les REQ
void gerer_requete(int socket_fd,int cmd,int nb_args, int process_id){

  int ressource=0;
  bool lecture = true;

  if(cmd==INIT){

    for(int j=0; j<nb_args; ++j){
      int len = read_socket(socket_fd,&ressource,sizeof(ressource),max_wait_time*1000);
      if (len > 0) {

        // Remplir les tableaux avec les donnees initiales
        remplir_donnees_initiales(process_id, ressource, j);

      } else {
        printf("Erreur de lecture...\n");
        lecture = false;
        break;
      }
    }

    for (int i = 0; i < count_ct ; ++i) {
      for (int j = 0; j < nombre_ressources; ++j) {
      }
    }
    if (lecture) {
      int ack[2] = {4, 0};
      send(socket_fd ,ack, sizeof(ack) ,0);
    }

  } else if(cmd==REQ){

    int *res;
    res = malloc(nb_args * sizeof(int));
    for(int i=0; i<nb_args; ++i){
      int len=read_socket(socket_fd,&ressource,sizeof(ressource),max_wait_time*1000);
      if (len > 0) {
        res[i] = ressource;

        // La ressource demande est plus grande que le max autorise
        // ou plus petit que (-)max autorise
        if ( res[i] > max[process_id][i] || res[i] < (max[process_id][i] * (-1)) ) {
          int err[3] = {ERR, 1, -1};
          send(socket_fd, err, sizeof(err), 0);
          free(res);
          break;
        }
        int *safe_sequence;
        safe_sequence = malloc(count_ct * sizeof(int));

        pthread_mutex_lock(&need_modifie);

        // verifier s'il est possible trouver une facon de faire
        if (safe_state(safe_sequence)) {
          // Ressource request algorithm


        } else {
          // repondre wait pour essayer plus tard
          //printf("TOO BAD!\n");
        }
        pthread_mutex_unlock(&need_modifie);





        free(safe_sequence);

      } else {
        printf("Erreur de lecture...\n");
        lecture = false;
        break;
      }
    }
    if (lecture) {
      int ack[2] = {4, 1};
      send(socket_fd ,ack, sizeof(ack) ,0);
    }
  }
  else{
    printf("ERR\n");
    int err[2] = {8, 0};
    send(socket_fd, &err, sizeof(err), 0);
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
    if (len != sizeof(header.cmd) && len != sizeof(header)) {
      printf ("Thread %d received invalid command size=%d!\n", st->id, len);
    } else {
      printf("\nThread %d received command=%d, nb_args=%d\n", st->id, header.cmd, header.nb_args);
      if (header.cmd != END) {

        if (header.nb_args > 0) {
          int process_id;
          len = read_socket(socket_fd, &process_id, sizeof(process_id), max_wait_time*1000);
          if (len > 0) {
            gerer_requete(socket_fd,header.cmd,header.nb_args, process_id);
          } else {
            printf("Erreur de lecture... \n");
          }


        }

      } else {
        // finir l'execution du thread serveur
      }

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