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
int nombre_ressources = 0;
int nombre_clients = 0;

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

int *total_ressources;
int **max;

int **allocation;
int **need;

int *available;
int *total_allocation;

//int count_ct = 0;


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

// TODO ERASE FONCTION
void tab_print (int **tab, char *tab_name, int lignes, int colonnes) {
  for (int i = 0; i < lignes; ++i) {
    for (int j = 0; j < colonnes; ++j) {
      printf("%s [%d][%d] = %d\n", tab_name, i, j, tab[i][j]);
    }
  }
}

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

void remplir_donnees_initiales (int process_id, int donne, int j) {
  pthread_mutex_lock(&max_modifie);
  max[process_id][j] = donne;   // maximum de ressources par client
  pthread_mutex_unlock(&max_modifie);

  pthread_mutex_lock(&allocation_modifie);
  allocation[process_id][j] = 0;    // Rien encore alloue
  pthread_mutex_unlock(&allocation_modifie);

  pthread_mutex_lock(&need_modifie);
  need[process_id][j] = max[process_id][j]; // Au debut need == max
  pthread_mutex_unlock(&need_modifie);
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
        nombre_clients = rng;
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
      printf("**************************Ressources*************************\n");

      nombre_ressources = header.nb_args;
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



void calculer_need () {

  //pthread_mutex_lock(&need_modifie);
  for (int i = 0; i < nombre_clients; ++i) {
    for (int j = 0; j < nombre_ressources; ++j) {
      need[i][j] = max[i][j] - allocation[i][j];
    }
  }
  //pthread_mutex_unlock(&need_modifie);
}

void calculer_available () {

  //pthread_mutex_lock(&available_modifie);
  for (int i = 0; i < nombre_ressources; ++i) {
    available[i] = total_ressources[i] - total_allocation[i];
  }
  //pthread_mutex_unlock(&available_modifie);

}

void calculer_total_allocation () {
  int add = 0;
  pthread_mutex_lock(&total_allocation_modifie);
  for (int i = 0; i < nombre_ressources; ++i) {
    add = 0;
    for (int j = 0; j < nombre_clients; ++j) {
      add += allocation[j][i];
    }
    total_allocation[i] = add;
  }
  pthread_mutex_unlock(&total_allocation_modifie);

}

bool safe_state (int nb_clients, int nb_ressources, int *nouvelle_available,
        int **nouvelle_need, int **nouvelle_allocation) {

  bool finish[nb_clients];
  int *work = malloc(nb_ressources * sizeof(int));

  for (int k = 0; k < nb_ressources; ++k) {
    work[k] = nouvelle_available[k];
  }
  for (int j = 0; j < nombre_clients; ++j) {
    finish[j] = false;
  }

  for (int i = 0; i < nombre_clients; ++i) {
    if (finish[i] == false) {
      for (int j = 0; j < nombre_ressources ; ++j) {
        if (nouvelle_need[i][j] <= work[j]) {
          work[j] = work[j] + nouvelle_allocation[i][j];
          finish[i] = true;
          break;
        }
      }
    }
  }
  free(work);
  for (int l = 0; l < nb_clients; ++l) {
    if (finish[l] == false) {
      printf("UNSAFE!\n");
      return false;
    }
  }
  return true;

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

    if (lecture) {
      int ack[2] = {4, 0};
      send(socket_fd ,ack, sizeof(ack) ,0);
    }

  } else if(cmd==REQ) {
    bool valide = true;
    int *ressources_demandes;
    int *safe_sequence;
    ressources_demandes = malloc(nombre_ressources * sizeof(int));
    safe_sequence = malloc(nombre_clients * sizeof(int));

    for (int i = 0; i < nb_args; ++i) {
      int len = read_socket(socket_fd, &ressource, sizeof(ressource), max_wait_time * 1000);
      if (len > 0) {

        // Array qui contient les ressources a allouer
        ressources_demandes[i] = ressource;

        // La ressource demande est plus grande que le max autorise
        // ou plus petit que (-)max autorise
        if (ressources_demandes[i] > total_ressources[i] || ressources_demandes[i] < (total_ressources[i] * (-1))) {
          printf("Erreur! Ressources demandes sont plus grandes que ressources totales.\n");
          int err[3] = {ERR, 1, -1};
          send(socket_fd, err, sizeof(err), 0);
          free(ressources_demandes);
          valide = false;
          break;
        }

      } else {
        printf("Erreur de lecture...\n");
        int err[3] = {ERR, 1, -1};
        send(socket_fd, err, sizeof(err), 0);
        valide = false;
        break;
      }

      int ack[2] = {ACK, 0};
      send(socket_fd, ack, sizeof(ack), 0);
    }

    if (valide) {

      for (int i = 0; i < nombre_ressources; ++i) {

        if (ressources_demandes[i] > need[process_id][i]) {
          //printf("Erreur! Nombre de ressources demande est plus grande que le maximum.\n");
          int err[3] = {ERR, 1, -1};
          send(socket_fd, err, sizeof(err), 0);
          break;
        } else {
          //printf("pas d'erreur\n");
        }

        if (ressources_demandes[i] <= available[i]) {
          int ct_wait[3] = {WAIT, 1, 2};
          //printf("Le client doit attendre %d secondes\n", ct_wait[2]);
          send(socket_fd, ct_wait, sizeof(ct_wait), 0);
        }

      }

      // Essayer d'allouer la ressource pour process_id pour calculer
      // un nouvel etat hypothetique

      int *nouvelle_available = malloc(nombre_ressources * sizeof(int));
      int **nouvelle_allocation = malloc(sizeof(int*) * nombre_clients);
      int **nouvelle_need = malloc(sizeof(int*) * nombre_clients);

      int *nouvelle_total_allocation = malloc(nombre_ressources * sizeof(int));

      pthread_mutex_lock(&available_modifie);
      for (int k = 0; k < nombre_clients; ++k) {
        nouvelle_available[k] = available[k] - ressources_demandes[k];
        }
      pthread_mutex_unlock(&available_modifie);

      for (int m = 0; m < nombre_clients ; ++m) {
        nouvelle_allocation[m] = malloc(nombre_ressources * sizeof(int));
        nouvelle_need[m] = malloc(nombre_ressources * sizeof(int));
      }

      pthread_mutex_lock(&allocation_modifie);
      for (int i = 0; i < nombre_clients ; ++i) {
        for (int j = 0; j < nombre_ressources; ++j) {
          if(i == process_id) {
            nouvelle_allocation[process_id][j] = allocation[process_id][j] + ressources_demandes[j];
            //printf("%d = %d + %d\n", nouvelle_allocation[process_id][j], allocation[process_id][j], ressources_demandes[j]);
          }
          nouvelle_allocation[i][j] = allocation[i][j];
        }
      }
      pthread_mutex_unlock(&allocation_modifie);


      pthread_mutex_lock(&total_allocation_modifie);
      for (int i1 = 0; i1 < nombre_ressources; ++i1) {
        nouvelle_total_allocation[i1] = total_allocation[i1] + ressources_demandes[i1];
      }
      pthread_mutex_unlock(&total_allocation_modifie);

      // verifier si le nouvelle_total_allocation[i] <= total_ressources[i]  et aussi
      // nouvelle_total_allocation[i] <= (-1) * total_ressources[i]


      pthread_mutex_lock(&need_modifie);
      for (int l = 0; l < nombre_clients; ++l) {
        for (int i = 0; i < nombre_ressources; ++i) {
          if (l == process_id) {
            nouvelle_need[process_id][i] = need[process_id][i] - ressources_demandes[i];
            //printf(" %d = %d + %d\n", nouvelle_need[process_id][i], need[process_id][i], ressources_demandes[i]);
          }
          nouvelle_need[l][i] = need[l][i];
        }
      }
      pthread_mutex_unlock(&need_modifie);


      safe_state (nombre_clients, nombre_ressources, nouvelle_available,
              nouvelle_need, nouvelle_allocation);






      //
      /*
       * Pour tous les j = 0, 1, 2, ... nombre_ressources
       *
       * On va creer un nouveau tableau nouveau_available avec la taille de available
       * on doit bloquer available
       * faire le calcul nouveau_available[j] = available - request[j];
       *
       * On va creer un nouveau tableau nouvelle_allocation avec la taille de available
       * on doit bloquer allocation
       * faire le calcul nouvelle_allocation[process_id][j] = allocation[process_id][j] + request[j];
       *
       * On va creer un nouveau tableau nouvelle_need avec la taille de available
       * on doit bloquer need
       * faire le calcul nouvelle_need[process_id][j] = need[process_id][j] - request[j];
       *
       * */


      for (int n = 0; n < nombre_ressources ; ++n) {
        free(nouvelle_allocation[n]);
        free(nouvelle_need[n]);
      }

      free(nouvelle_total_allocation);
      free(nouvelle_need);
      free(nouvelle_allocation);
      free(nouvelle_available);
      free(safe_sequence);
      free(ressources_demandes);

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