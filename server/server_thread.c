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
int wait_time = 0;

int *total_ressources;
int **max;

int **allocation;
int **need;

int *available;
int *total_allocation;

int *requete_en_attend;


// Mutex pour la modificacion des nombre de requetes
pthread_mutex_t accepte_sans_delai;
pthread_mutex_t accepte_avec_delai;
pthread_mutex_t erreur_apres_requete;
pthread_mutex_t client_finis_correctement;
pthread_mutex_t requetes_proceses;
pthread_mutex_t client_ferme;
pthread_mutex_t en_attendant;


// Mutex pour l'algorithme du bankier
pthread_mutex_t max_modifie;
pthread_mutex_t allocation_modifie;
pthread_mutex_t need_modifie;
pthread_mutex_t bankers_algo;


/********************************************************************************************************************/

// TODO ERASE START
void tab_print_2d (int **tab, char *tab_name) {
  for (int i = 0; i < nombre_clients; ++i) {
    for (int j = 0; j < nombre_ressources; ++j) {
      printf("%s [%d][%d] = %d   -   ", tab_name, i, j, tab[i][j]);
    }
    printf("\n");
  }
  printf("\n\n");
}

void tab_print (int *tab, char *tab_name) {
  for (int i = 0; i < nombre_ressources; ++i) {
    printf("%s [%d] = %d   -   ", tab_name, i, tab[i]);
  }
  printf("\n\n");
}

// TODO ERASE END

void sans_delai() {
  pthread_mutex_lock(&accepte_sans_delai);
  count_accepted++;
  pthread_mutex_unlock(&accepte_sans_delai);
}

void avec_delai() {
  pthread_mutex_lock(&accepte_avec_delai);
  count_wait++;
  pthread_mutex_unlock(&accepte_avec_delai);
}

void erreur_envoye() {
  pthread_mutex_lock(&erreur_apres_requete);
  count_invalid++;
  pthread_mutex_unlock(&erreur_apres_requete);
}

void client_fini() {
  pthread_mutex_lock(&client_finis_correctement);
  count_dispatched++;
  pthread_mutex_unlock(&client_finis_correctement);
}

void requetes_total () {
  pthread_mutex_lock(&requetes_proceses);
  request_processed++;
  pthread_mutex_unlock(&requetes_proceses);
}

void clo_recu() {
  pthread_mutex_lock(&client_ferme);
  clients_ended++;
  pthread_mutex_unlock(&client_ferme);
}

void initialiser_tableaux (int nb_clients, int nb_ressources) {

  total_ressources = malloc(sizeof(int) * nb_ressources);
  total_allocation = malloc(sizeof(int) * nb_ressources);
  max = malloc(sizeof(int*) * nb_clients);
  allocation = malloc(sizeof(int*) * nb_clients);
  need = malloc(sizeof(int*) * nb_clients);

  available = malloc(nb_ressources * sizeof(int));

  requete_en_attend = malloc(nb_ressources * sizeof(int));

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
    max[i] = NULL;
    free(allocation[i]);
    allocation[i] = NULL;
    free(need[i]);
    need[i] = NULL;
  }

  free(available);
  available = NULL;
  free(total_ressources);
  total_ressources = NULL;
  free(total_allocation);
  total_allocation = NULL;
  free(max);
  max = NULL;
  free(allocation);
  allocation = NULL;
  free(need);
  need = NULL;
  free(requete_en_attend);
  requete_en_attend = NULL;
}

/* Initialiser tous les casses de max[ct_id][j], allocation[ct_id][j] et
 * need[ct_id][j]
 * */
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
      int len2 = read_socket(socket_fd, &rng, sizeof(rng), max_wait_time * 1000);
      if (len2 > 0) {
        nombre_clients = rng;
        printf("BEGIN nb.args=%d  rng=%d\n",header.nb_args, rng);
        int ack[3]={ACK,1, rng};
        send(socket_fd ,ack, sizeof(ack) ,0);
      } else {
        printf("Erreur dans RNG");
        int err[3] = {ERR, 1, -1};
        send(socket_fd, err, sizeof(err), 0);
      }
    } else {
      printf("\nLe serveur ne peut pas etre initialisé\n");
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
 * Fonction qui recoit la configuration initial du serveur et qui va ajouter
 * le maximum des ressources disponibles pour les clients.
 * */
int recevoir_ressource(int socket_fd){
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
        int reponse[1]={ACK};
        send(socket_fd ,&reponse, sizeof(reponse) ,0);
      }
      // Initialisation des tableaux auxiliaires
      for (int j = 0; j < nombre_ressources; ++j) {
        total_allocation[j] = 0;
        available[j] = total_ressources[j];
      }
    } else {
      printf("ERR\n");
      stat = -1;
    }
  } else {
    printf("Echec dans la configuration...\n");
    stat = -1;
  }

  for (int k = 0; k < nombre_ressources; ++k) {
    requete_en_attend[k] = 0;
  }
  close(socket_fd);
  return stat;
}

void calcul_tab(int lignes, int colonnes, int p_id,int **nouvelle_alloc, int *ress) {
  for (int i = 0; i < lignes; ++i) {
    for (int j = 0; j < colonnes; ++j) {
      if (i == p_id) {
        nouvelle_alloc[p_id][j] = allocation[p_id][j] + ress[j];
      }
      nouvelle_alloc[i][j] = allocation[i][j];
    }
  }
}


void calcul_tab2(int lignes, int colonnes, int p_id,int **nouvelle_need, int *ress) {
  for (int i = 0; i < lignes; ++i) {
    for (int j = 0; j < colonnes; ++j) {
      if (i == p_id) {
        nouvelle_need[p_id][j] = need[p_id][j] + ress[j];
      }
      nouvelle_need[i][j] = need[i][j];
    }
  }
}

/*
 * Fonction qui va vérifier si dans l'etat actuelle il est possible
 * de générer un deadlock si on fait les allocation necessaires pour
 * les clients.
 * */
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
      return false;
    }
  }
  return true;

}

//int socket_fd, int cmd, int nb_args, int process_id
void gerer_init(int socket_fd, int nb_args, int process_id){
  int ressource=0;
  bool lecture = true;

  for(int j=0; j<nb_args; ++j){
    int len = read_socket(socket_fd,&ressource,sizeof(ressource), max_wait_time*1000);
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
}

void gerer_requete(int socket_fd, int nb_args, int process_id){

  int ressource=0;
  bool valide = true;
  bool wait = false;

  if (requete_en_attend[process_id] == 0) {
    requetes_total();
  }

  int *ressources_demandes = malloc(nombre_ressources * sizeof(int));


  for (int i = 0; i < nb_args; ++i) {
    int len = read_socket(socket_fd, &ressource, sizeof(ressource), max_wait_time * 1000);
    if (len > 0) {

      // Array qui contient les ressources a allouer
      ressources_demandes[i] = ressource;

      // La ressource demande est plus grande que le max autorise
      // ou plus petit que (-)max autorise
      if (ressources_demandes[i] > total_ressources[i] || ressources_demandes[i] < (total_ressources[i] * (-1))) {
        printf("Les ressources demandes sont plus grandes que ressources totales.\n");
        int err[3] = {ERR, 1, -1};
        send(socket_fd, err, sizeof(err), 0);
        erreur_envoye();
        valide = false;
        break;
      }

      if (available[i] - ressources_demandes[i] < total_ressources[i]) {
        printf("Les ressources demandes depassent le total disponible pour les clients\n");
        int err[3] = {ERR, 1, -1};
        send(socket_fd, err, sizeof(err), 0);
        erreur_envoye();
        valide = false;
        break;
      }

      if ( available[i] - ressources_demandes[i] < 0 ) {
        printf("Les ressources demandes ne sont pas disponibles en ce moment...\n");
        wait = true;
        break;
      }

      if (allocation[process_id][i] + ressources_demandes[i] > max[process_id][i]) {
        printf("Les ressources demandes depassent le maximum permit au client\n");
        int err[3] = {ERR, 1, -1};
        send(socket_fd, err, sizeof(err), 0);
        erreur_envoye();
        valide = false;
        break;
      }

      if (need[process_id][i] - ressources_demandes[i] < 0) {
        printf("Le client a demande plus de ressources qui est necessaire\n\n");
        int err[3] = {ERR, 1, -1};
        send(socket_fd, err, sizeof(err), 0);
        erreur_envoye();
        valide = false;
        break;
      }

    } else {
      printf("Erreur de lecture...\n");
      int err[3] = {ERR, 1, -1};
      send(socket_fd, err, sizeof(err), 0);
      erreur_envoye();
      valide = false;
      break;
    }
  }

  if (valide) {

    if (wait) {
      int ct_wait[3] = {WAIT, 1, wait_time};
      send(socket_fd, ct_wait, sizeof(ct_wait), 0);
      free(ressources_demandes);
      requete_en_attend[process_id] = 1;
    } else {


      // ALgorithme du banquier

      // Essayer d'allouer les ressources pour process_id pour calculer
      // un nouvel etat hypothetique

      pthread_mutex_lock(&bankers_algo);
      int *nouvelle_available = malloc(nombre_ressources * sizeof(int));
      int **nouvelle_allocation = malloc(sizeof(int*) * nombre_clients);
      int **nouvelle_need = malloc(sizeof(int*) * nombre_clients);
      int *nouvelle_total_allocation = malloc(nombre_ressources * sizeof(int));

      // Allocation 2D
      for (int m = 0; m < nombre_clients ; ++m) {
        nouvelle_allocation[m] = malloc(nombre_ressources * sizeof(int));
        nouvelle_need[m] = malloc(nombre_ressources * sizeof(int));
      }

      for (int k = 0; k < nombre_ressources; ++k) {
        nouvelle_available[k] = available[k] - ressources_demandes[k];
      }
      calcul_tab(nombre_clients, nombre_ressources, process_id, nouvelle_allocation, ressources_demandes);
      //pthread_mutex_unlock(&allocation_modifie);


      for (int i1 = 0; i1 < nombre_ressources; ++i1) {
        nouvelle_total_allocation[i1] = total_allocation[i1] + ressources_demandes[i1];
      }
      calcul_tab2(nombre_clients, nombre_ressources, process_id, nouvelle_need, ressources_demandes);

      if (safe_state (nombre_clients, nombre_ressources, nouvelle_available,
                      nouvelle_need, nouvelle_allocation) ) {

        // Modifications dans les tableaux principaux
        for (int i = 0; i < nombre_ressources; ++i) {
          available[i] = available[i] - ressources_demandes[i];
        }

        for (int j = 0; j < nombre_ressources; ++j) {
          allocation[process_id][j] = allocation[process_id][j] + ressources_demandes[j];
        }

        for (int j = 0; j < nombre_ressources; ++j) {
          need[process_id][j] = need[process_id][j] - ressources_demandes[j];
        }

        for (int i1 = 0; i1 < nombre_ressources; ++i1) {
          total_allocation[i1] = total_allocation[i1] + ressources_demandes[i1];
        }

        int ack[2] = {ACK, 0};
        send(socket_fd, ack, sizeof(ack), 0);
        if (requete_en_attend[process_id] == 1) {
          requete_en_attend[process_id] = 0;
          avec_delai();
        } else {
          sans_delai();
        }

      } else {
        int ct_wait[3] = {WAIT, 1, wait_time};
        //printf("Le client doit attendre %d secondes\n", ct_wait[2]);
        send(socket_fd, ct_wait, sizeof(ct_wait), 0);
        pthread_mutex_lock(&en_attendant);
        requete_en_attend[process_id] = 1;
        pthread_mutex_unlock(&en_attendant);
      }
      pthread_mutex_unlock(&bankers_algo);

      // Libération des ressources
      for (int n = 0; n < nombre_ressources ; ++n) {
        free(nouvelle_allocation[n]);
        nouvelle_allocation[n] = NULL;
        free(nouvelle_need[n]);
        nouvelle_need[n] = NULL;
      }

      free(nouvelle_total_allocation);
      nouvelle_total_allocation = NULL;
      free(nouvelle_need);
      nouvelle_need = NULL;
      free(nouvelle_allocation);
      nouvelle_allocation = NULL;
      free(nouvelle_available);
      nouvelle_available = NULL;
      free(ressources_demandes);
      ressources_demandes = NULL;
    }
  } else {
    free(ressources_demandes);
  }
}

void gerer_clo (int socket_fd, int nb_args, int process_id) {
  clo_recu();
  // Le client annonce la fin

  // Le serveur cherche les donne du client qui se trouvent dans les tableaux allocation et need
  // bloquer chaque tableau et faire la modification pour
  // tab[process_id][j] = 0; pout tout j = 0, 1, ... nombre_de_ressources
  // bloquer les tableaux de available et calculer nouvelle_available[i]
  //
  int ack[2] = {ACK, 0};
  send(socket_fd, ack, sizeof(ack), 0);
  client_fini();
}

void gerer_end (int socket_fd, int nb_args, int process_id) {

  // TODO le derniere client doit arreter le serveur (egal a BEGIN)

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
      int process_id;
      len = read_socket(socket_fd, &process_id, sizeof(process_id), max_wait_time*1000);
      if (len > 0) {
        if (header.cmd == INIT) {
          gerer_init(socket_fd, header.nb_args, process_id);
        } else if (header.cmd == REQ) {
          gerer_requete(socket_fd, header.nb_args, process_id);
        } else if (header.cmd == CLO) {
          gerer_clo(socket_fd, header.nb_args, process_id);
        } else if (header.cmd == END) {
          gerer_end(socket_fd, header.nb_args, process_id);
        } else {
          printf("La commande n'est pas encore supporte\n");
          int err[3] = {ERR, 1, -1};
          send(socket_fd, err, sizeof(err), 0);
          erreur_envoye();
        }

      } else {
        printf("Erreur de lecture... \n");
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

  // TODO Deplacer vers une autre endroit et modifier car ca va generer des bugs si tous les clients ne sont pas termines
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