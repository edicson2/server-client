/* This `define` tells unistd to define usleep and random.  */
#define _XOPEN_SOURCE 500

#include "client_thread.h"
#include <netinet/in.h>

int port_number = -1;
int num_request_per_client = -1;
int num_resources = -1;
int *provisioned_resources = NULL;

// Variable d'initialisation des threads clients.
unsigned int count = 0;


// Variable du journal.
// Nombre de requête acceptée (ACK reçus en réponse à REQ)
unsigned int count_accepted = 0;

// Nombre de requête en attente (WAIT reçus en réponse à REQ)
unsigned int count_on_wait = 0;

// Nombre de requête refusée (REFUSE reçus en réponse à REQ)
unsigned int count_invalid = 0;

// Nombre de client qui se sont terminés correctement (ACC reçu en réponse à END)
unsigned int count_dispatched = 0;

// Nombre total de requêtes envoyées.
unsigned int request_sent = 0;


// Vous devez modifier cette fonction pour faire l'envoie des requêtes
// Les ressources demandées par la requête doivent être choisies aléatoirement
// (sans dépasser le maximum pour le client). Elles peuvent être positives
// ou négatives.
// Assurez-vous que la dernière requête d'un client libère toute les ressources
// qu'il a jusqu'alors accumulées.
void
send_request (int client_id, int request_id, int socket_fd)
{

  // TP2 TODO

  fprintf (stdout, "Client %d is sending its %d request\n", client_id,
      request_id);

  // TP2 TODO:END

}


void *
ct_code (void *param)
{
  int socket_fd = -1;
  client_thread *ct = (client_thread *) param;


  // TP2 TODO
  // Vous devez ici faire l'initialisation des petits clients (`INI`).

  // creation du socket pour établir une connection
  socket_fd = (AF_INET, SOCK_STREAM, 0);
  
  // addresse pour le socket
  struct sockaddr_in address_serveur;
  address_serveur.sin_family = AF_INET;
  address_serveur.sin_port = htons( port_number ); 
  address_serveur.sin_addr.s_addr = INADDR_ANY;

  // obtain certain int qu'indique si la connection est fait avec success
  int connection_status = connect(socket_fd, (struct sockaddr *) &address_serveur, sizeof(address_serveur) );

  if (connection_status == -1)
  {
    printf("Echec de la connection avec le socket\n");
  }

  char reponse_serveur[256];
  recv( socket_fd, &reponse_serveur, sizeof(reponse_serveur), 0 );

  // afficher la reponse du serveur
  printf("Serveur reponse : %s\n", reponse_serveur );

  close(socket_fd);

  // TP2 TODO:END

  for (unsigned int request_id = 0; request_id < num_request_per_client;
      request_id++)
  {

    // TP2 TODO
    // Vous devez ici coder, conjointement avec le corps de send request,
    // le protocole d'envoi de requête.

    send_request (ct->id, request_id, socket_fd);

    // TP2 TODO:END

    /* Attendre un petit peu (0s-0.1s) pour simuler le calcul.  */
    usleep (random () % (100 * 1000));
    /* struct timespec delay;
     * delay.tv_nsec = random () % (100 * 1000000);
     * delay.tv_sec = 0;
     * nanosleep (&delay, NULL); */
  }

  pthread_exit (NULL);
}


//
// Vous devez changer le contenu de cette fonction afin de régler le
// problème de synchronisation de la terminaison.
// Le client doit attendre que le serveur termine le traitement de chacune
// de ses requêtes avant de terminer l'exécution.
//
void
ct_wait_server ()
{

  // TP2 TODO

  sleep (4);

  // TP2 TODO:END

}


void
ct_init (client_thread * ct)
{
  ct->id = count++;
}

void
ct_create_and_start (client_thread * ct)
{
  pthread_attr_init (&(ct->pt_attr));
  pthread_attr_setdetachstate(&(ct->pt_attr), PTHREAD_CREATE_DETACHED);
  pthread_create (&(ct->pt_tid), &(ct->pt_attr), &ct_code, ct);
}

//
// Affiche les données recueillies lors de l'exécution du
// serveur.
// La branche else ne doit PAS être modifiée.
//
void
st_print_results (FILE * fd, bool verbose)
{
  if (fd == NULL)
    fd = stdout;
  if (verbose)
  {
    fprintf (fd, "\n---- Résultat du client ----\n");
    fprintf (fd, "Requêtes acceptées: %d\n", count_accepted);
    fprintf (fd, "Requêtes : %d\n", count_on_wait);
    fprintf (fd, "Requêtes invalides: %d\n", count_invalid);
    fprintf (fd, "Clients : %d\n", count_dispatched);
    fprintf (fd, "Requêtes envoyées: %d\n", request_sent);
  }
  else
  {
    fprintf (fd, "%d %d %d %d %d\n", count_accepted, count_on_wait,
        count_invalid, count_dispatched, request_sent);
  }
}
