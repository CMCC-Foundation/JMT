/***************************************
 *     Job Memory Thresh (JMT) v1.0    *
 ***************************************
 *                                     *
 *         ██╗███╗   ███╗████████╗     *
 *         ██║████╗ ████║╚══██╔══╝     *
 *         ██║██╔████╔██║   ██║        *
 *    ██   ██║██║╚██╔╝██║   ██║        *
 *    ╚█████╔╝██║ ╚═╝ ██║   ██║        *
 *     ╚════╝ ╚═╝     ╚═╝   ╚═╝        *
 *                                     *
 ***************************************
 * License: AGPL v3.
 * Giuseppe Calò                giuseppe.calo@cmcc.it
 * Danilo Mazzarella            danilo.mazzarella@cmcc.it 
 * Marco Chiarelli              marco.chiarelli@cmcc.it
 *                              marco_chiarelli@yahoo.it
*/

#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>


#define STRINGIFY(s) _STRINGIFY(s)
#define _STRINGIFY(s) #s

#define PADDING "                                                     " // "#####################################################"
// #define BORDER  "-----------------------------------------------------" 
#define BORDER "----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------"

// #define DEBUG_MODE
#define JOB_MEM_THRESH "CALL job_mem_thresh(?,?,?,?,?,?)"

#define MEM_LOWER_LIMIT 70
#define MEM_UPPER_LIMIT 150


#define DEFAULT_LOWER_DATE "2022-12-19"
#define DEFAULT_UPPER_DATE "2023-03-27"
#define DEFAULT_MIN_RUSAGE_MEM 3072
#define DEFAULT_MIN_DISCRIMINANT_LCSS_JOBNAME_LENGTH 3

#define ACCT_SERVER "127.0.0.1"
#define ACCT_USER "root"
#define ACCT_PASSWORD "root"
#define ACCT_DATABASE "database"


enum 
{
	P_LOWER_MEM,
	P_UPPER_MEM,
	P_MIN_RUSAGE_MEM,
	P_MIN_DISCRIMINANT_LCSS_JOBNAME_LENGTH,
	P_LOWER_DATE,
	P_UPPER_DATE,
	MAX_IN_PARAMS
} mem_in_p;

#define DEFAULT_MAIL_COMMAND "sendmail"
#define DEFAULT_FROM_MAIL "job_mem_tresh"
#define MAX_MAILCMDTO_LEN 128
#define MAX_LOGBUF_LEN 130000
#define MAX_MAIL_BUF 160000
#define MAX_TIME_LEN 100

#define MAX_USERNAME_LEN 16
#define MAX_JOBNAME_LEN 256
#define MAX_QUEUE_LEN 16
#define MAX_APP_LEN 20
#define MAX_SLA_LEN 50
#define MAX_PROJECT_LEN 16

#define _MAX_JOBNAME_LEN MAX_JOBNAME_LEN // 20
#define _MAX_SLA_LEN 20

#define MAX_BUFLINE_LEN 3*(30+MAX_USERNAME_LEN+MAX_QUEUE_LEN+MAX_APP_LEN+_MAX_SLA_LEN+MAX_PROJECT_LEN+_MAX_JOBNAME_LEN) // 2*...
#define NUMBERS_FIXED_LEN 21 // 16

enum
{
        P_USERNAME,
        P_QUEUE,
        P_APP,
        P_SLA,
        P_PROJECT,
        P_JOBNAME,
        P_MIN_MAXRMEM,
        P_MAX_MAXRMEM,
        P_AVG_MAXRMEM,
        P_MIN_RUSAGE,
        P_MAX_RUSAGE,
        P_AVG_RUSAGE,
	P_MIN_RATIO,
	P_MAX_RATIO,
	P_AVG_RATIO,
	P_NJOBS,
	MAX_OUT_PARAMS
} mem_out_p;

static void sendmail(const char * from_mail, const char * to_mail, const char * mail_cmd, const char * message)
{
	FILE * pp;
	static char buf[MAX_MAIL_BUF]; 

	sprintf(buf, "Subject: JMT Report - Job Mem Thresh\n"
		     "From: %s\n"
		     "To: %s\n"
		     "Mime-Version: 1.0\n"
		     "Content-Type: multipart/related; boundary=\"boundary-example\"; type=\"text/html\"\n"
		     "\n"
		     "--boundary-example\n"
		     "Content-Type: text/html; charset=ISO-8859-15\n"
		     "Content-Transfer-Encoding: 7bit\n"
		     "\n"
		     "<html>"
		     "<head>"
		     "<meta http-equiv=\"content-type\" content=\"text/html; charset=ISO-8859-15\">"
		     "</head>"
		     "<body bgcolor=\"#ffffff\" text=\"#000000\">"
		     "<font face=\"Courier New\">%s</font><br><br><br><br><br>" 
                     "Best Regards,<br>Marco Chiarelli, Danilo Mazzarella.<br><br>"
		     "</body>\n"
		     "</html>\n"	
		    "--boundary-example\n\n", from_mail, to_mail, message);

	if((pp = popen(mail_cmd, "w")) == NULL)
        {
                // fencing_fault = LFR_IO;
                fprintf(stderr, "Cannot send mail through the %s command.\n", mail_cmd);
		return;
	}

	// fwrite(message, 1, strlen(message)+1, pp);
	fprintf(pp, buf); // message);
	pclose(pp);
	return;
}

int main(int argc, char *argv[])
{
    const short int mem_lower_limit = argc > 1 ? atoi(argv[1]) : MEM_LOWER_LIMIT; 
    const short int mem_upper_limit = argc > 2 ? atoi(argv[2]) : MEM_UPPER_LIMIT;
    const short int min_rusage_mem = argc > 3 ? atoi(argv[3]) : DEFAULT_MIN_RUSAGE_MEM;
    const short int min_discriminant_lcss_jobname_length = argc > 4 ? atoi(argv[4]) : DEFAULT_MIN_DISCRIMINANT_LCSS_JOBNAME_LENGTH;
    const char * p_lower_date = argc > 5 ? argv[5] : DEFAULT_LOWER_DATE;
    const char * p_upper_date = argc > 6 ? argv[6] : DEFAULT_UPPER_DATE;
    const char * server = argc > 7 ? argv[7] : ACCT_SERVER;
    const char * user = argc > 8 ? argv[8] : ACCT_USER;
    const char * password = argc > 9 ? argv[9] : ACCT_PASSWORD;
    const char * database = argc > 10 ? argv[10] : ACCT_DATABASE;
    const char * mail_cmd = argc > 11 ? argv[11] : DEFAULT_MAIL_COMMAND;
    const char * from_mail = argc > 12 ? argv[12] : DEFAULT_FROM_MAIL;
    const char * to_mail = argc > 13 ? argv[13] : NULL;

    char mail_cmd_to[MAX_MAILCMDTO_LEN];
        
    if(to_mail)
        sprintf(mail_cmd_to, "%s \"%s\"", mail_cmd, to_mail);

    // printf("argc: %d\n", argc);

    #define is_mail_active (from_mail && to_mail)

    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;

    // char mail_cmd_to[MAX_MAILCMDTO_LEN];

    conn = mysql_init(NULL);

    //apro la connessione al DB
    if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0))
    {
        fprintf(stderr, "Error on mysql_real_connect!\n");
        exit(1);
    }

    MYSQL_STMT *stmt;
    MYSQL_RES  *prepare_meta_result;
    MYSQL_BIND ps_params[MAX_IN_PARAMS];
    MYSQL_BIND out_params[MAX_OUT_PARAMS];

    int padLens[6];
    int numbersPadLens[10];
    const char * padding = PADDING; //  "#####################################################";
    const char * border_padding = BORDER; 
    char p_numbers[10][33];
    char buffer[MAX_BUFLINE_LEN];
    char border_buffer[MAX_BUFLINE_LEN];
    char mail_buffer[MAX_LOGBUF_LEN];

    /* initialize and prepare CALL statement with parameter placeholders */
    #ifdef DEBUG_MODE
    printf("\nBefore mysql_stmt_init()\n");
    #endif
    stmt = mysql_stmt_init(conn);
    if (!stmt)
    {
        printf("\nerror when init statement\n");
        mysql_close(conn);
        exit(1);
    }

    #ifdef DEBUG_MODE
    printf("After mysql_stmt_init() and before mysql_stmt_prepare\n");
    #endif

    int status = mysql_stmt_prepare(stmt, JOB_MEM_THRESH, strlen(JOB_MEM_THRESH));

    #ifdef DEBUG_MODE
    printf("\nstatus of stmt prepare: %d\n", status);
    printf("After mysql_stmt_prepare and before memset\n");
    #endif

    /* initialize parameters: p_in, p_out, p_inout (all INT) */
    memset(ps_params, 0, sizeof(ps_params));
    memset(out_params, 0, sizeof(out_params));

    #ifdef DEBUG_MODE
    printf("\n\n *** AFTER MEMSET AND BEFORE ALL PS_PARAMS STUFFS *** \n\n");
    #endif

    unsigned long ul_zero_value = 0;

    // lower
    short p_lower_mem = 0;
    ps_params[P_LOWER_MEM].buffer_type = MYSQL_TYPE_SHORT;
    ps_params[P_LOWER_MEM].buffer = (short *) &p_lower_mem;
    ps_params[P_LOWER_MEM].length = &ul_zero_value;
    ps_params[P_LOWER_MEM].is_null = 0;

    // upper
    short p_upper_mem = 0;
    ps_params[P_UPPER_MEM].buffer_type = MYSQL_TYPE_SHORT;
    ps_params[P_UPPER_MEM].buffer = (short *) &p_upper_mem;
    ps_params[P_UPPER_MEM].length = &ul_zero_value;
    ps_params[P_UPPER_MEM].is_null = 0;

    // min_rusage_mem
    short p_min_rusage_mem = 0;
    ps_params[P_MIN_RUSAGE_MEM].buffer_type = MYSQL_TYPE_SHORT;
    ps_params[P_MIN_RUSAGE_MEM].buffer = (short *) &p_min_rusage_mem;
    ps_params[P_MIN_RUSAGE_MEM].length = &ul_zero_value;
    ps_params[P_MIN_RUSAGE_MEM].is_null = 0;

    // min_discriminant_jobname_lcss_length
    short p_min_discriminant_lcss_jobname_length = 0;
    ps_params[P_MIN_DISCRIMINANT_LCSS_JOBNAME_LENGTH].buffer_type = MYSQL_TYPE_SHORT;
    ps_params[P_MIN_DISCRIMINANT_LCSS_JOBNAME_LENGTH].buffer = (short *) &p_min_discriminant_lcss_jobname_length;
    ps_params[P_MIN_DISCRIMINANT_LCSS_JOBNAME_LENGTH].length = &ul_zero_value;
    ps_params[P_MIN_DISCRIMINANT_LCSS_JOBNAME_LENGTH].is_null = 0;
   
    // my_bool is_null_starttime;
  
    // lower_date
    unsigned long p_lower_date_length = strlen(p_lower_date);
    ps_params[P_LOWER_DATE].buffer_type = MYSQL_TYPE_STRING;
    ps_params[P_LOWER_DATE].buffer = (char *) p_lower_date;
    ps_params[P_LOWER_DATE].buffer_length = MAX_TIME_LEN;
    ps_params[P_LOWER_DATE].length = &p_lower_date_length;
    ps_params[P_LOWER_DATE].is_null = 0;

    // upper_date
    unsigned long p_upper_date_length = strlen(p_upper_date);
    ps_params[P_UPPER_DATE].buffer_type = MYSQL_TYPE_STRING;
    ps_params[P_UPPER_DATE].buffer = (char *) p_upper_date;
    ps_params[P_UPPER_DATE].buffer_length = MAX_TIME_LEN;
    ps_params[P_UPPER_DATE].length = &p_upper_date_length;
    ps_params[P_UPPER_DATE].is_null = 0;
    

    /* preparing output buffer */
    if((status = mysql_stmt_bind_param(stmt, ps_params)))
    {
        fprintf(stderr, "mysql_stmt_bind_param failed\n");
        exit(1);
    }

    #ifdef DEBUG_MODE
    printf("mem_lower_limit: %d\nmem_upper_limit: %d\n", mem_lower_limit, mem_upper_limit);
    #endif

    p_lower_mem = mem_lower_limit;
    p_upper_mem = mem_upper_limit;
    p_min_rusage_mem = min_rusage_mem;
    p_min_discriminant_lcss_jobname_length = min_discriminant_lcss_jobname_length;

    if ((status = mysql_stmt_execute(stmt)))
    {
        fprintf(stderr, "execution of statement failed\n");
        exit(1);
    }

    #ifdef DEBUG_MODE
    int num_fields = mysql_stmt_field_count(stmt);
    printf("num_fields: %d\n", num_fields);
    #endif

    // username
    unsigned long p_username_length = 0;
    char p_username[MAX_USERNAME_LEN];
    out_params[P_USERNAME].buffer_type = MYSQL_TYPE_STRING; 
    out_params[P_USERNAME].buffer = (char *) p_username; 
    out_params[P_USERNAME].buffer_length = MAX_USERNAME_LEN;
    out_params[P_USERNAME].is_null = 0;
    out_params[P_USERNAME].length = &p_username_length;
    
    // queue
    unsigned long p_queue_length = 0;
    char p_queue[MAX_QUEUE_LEN]; // NULL;
    out_params[P_QUEUE].buffer_type = MYSQL_TYPE_STRING;
    out_params[P_QUEUE].buffer = (char *) p_queue;
    out_params[P_QUEUE].buffer_length = MAX_QUEUE_LEN;
    out_params[P_QUEUE].length = &p_queue_length;
    out_params[P_QUEUE].is_null = 0;

    // app
    unsigned long p_app_length = 0;
    char p_app[MAX_APP_LEN]; // NULL;
    out_params[P_APP].buffer_type = MYSQL_TYPE_STRING;
    out_params[P_APP].buffer = (char *) p_app;
    out_params[P_APP].buffer_length = MAX_APP_LEN;
    out_params[P_APP].length = &p_app_length;
    out_params[P_APP].is_null = 0;

    // serviceclass
    unsigned long p_sla_length = 0;
    char p_sla[MAX_SLA_LEN]; // NULL;
    out_params[P_SLA].buffer_type = MYSQL_TYPE_STRING;
    out_params[P_SLA].buffer = (char *) p_sla;
    out_params[P_SLA].buffer_length = MAX_SLA_LEN;
    out_params[P_SLA].length = &p_sla_length;
    out_params[P_SLA].is_null = 0;

    // project
    unsigned long p_project_length = 0;
    char p_project[MAX_PROJECT_LEN]; // NULL;
    out_params[P_PROJECT].buffer_type = MYSQL_TYPE_STRING;
    out_params[P_PROJECT].buffer = (char *) p_project;
    out_params[P_PROJECT].buffer_length = MAX_PROJECT_LEN;
    out_params[P_PROJECT].length = &p_project_length;
    out_params[P_PROJECT].is_null = 0;

    // jobName
    unsigned long p_jobname_length = 0;
    char p_jobname[MAX_JOBNAME_LEN]; // NULL;
    out_params[P_JOBNAME].buffer_type = MYSQL_TYPE_STRING;
    out_params[P_JOBNAME].buffer = (char *) p_jobname;
    out_params[P_JOBNAME].buffer_length = MAX_JOBNAME_LEN;
    out_params[P_JOBNAME].length = &p_jobname_length;
    out_params[P_JOBNAME].is_null = 0;

    // min_maxRMem
    float p_min_maxrmem = 0;
    out_params[P_MIN_MAXRMEM].buffer_type = MYSQL_TYPE_FLOAT;
    out_params[P_MIN_MAXRMEM].buffer = (float *) &p_min_maxrmem;
    out_params[P_MIN_MAXRMEM].length = &ul_zero_value;
    out_params[P_MIN_MAXRMEM].is_null = 0;
    
    // max_maxRMem
    float p_max_maxrmem = 0;
    out_params[P_MAX_MAXRMEM].buffer_type = MYSQL_TYPE_FLOAT;
    out_params[P_MAX_MAXRMEM].buffer = (float *) &p_max_maxrmem;
    out_params[P_MAX_MAXRMEM].length = &ul_zero_value;
    out_params[P_MAX_MAXRMEM].is_null = 0;
    
    // avg_maxRMem
    float p_avg_maxrmem = 0;
    out_params[P_AVG_MAXRMEM].buffer_type = MYSQL_TYPE_FLOAT;
    out_params[P_AVG_MAXRMEM].buffer = (float *) &p_avg_maxrmem;
    out_params[P_AVG_MAXRMEM].length = &ul_zero_value;
    out_params[P_AVG_MAXRMEM].is_null = 0;
    
    // min_rusage
    float p_min_rusage = 0;
    out_params[P_MIN_RUSAGE].buffer_type = MYSQL_TYPE_FLOAT;
    out_params[P_MIN_RUSAGE].buffer = (float *) &p_min_rusage;
    out_params[P_MIN_RUSAGE].length = &ul_zero_value;
    out_params[P_MIN_RUSAGE].is_null = 0;
    
    // minrusage
    float p_max_rusage = 0;
    out_params[P_MAX_RUSAGE].buffer_type = MYSQL_TYPE_FLOAT;
    out_params[P_MAX_RUSAGE].buffer = (float *) &p_max_rusage;
    out_params[P_MAX_RUSAGE].length = &ul_zero_value;
    out_params[P_MAX_RUSAGE].is_null = 0;
    
    // minrusage
    float p_avg_rusage = 0;
    out_params[P_AVG_RUSAGE].buffer_type = MYSQL_TYPE_FLOAT;
    out_params[P_AVG_RUSAGE].buffer = (float *) &p_avg_rusage;
    out_params[P_AVG_RUSAGE].length = &ul_zero_value;
    out_params[P_AVG_RUSAGE].is_null = 0;
    
    // min_ratio
    float p_min_ratio = 0;
    out_params[P_MIN_RATIO].buffer_type = MYSQL_TYPE_FLOAT;
    out_params[P_MIN_RATIO].buffer = (float *) &p_min_ratio;
    out_params[P_MIN_RATIO].length = &ul_zero_value;
    out_params[P_MIN_RATIO].is_null = 0;

    // ratio
    float p_max_ratio = 0;
    out_params[P_MAX_RATIO].buffer_type = MYSQL_TYPE_FLOAT;
    out_params[P_MAX_RATIO].buffer = (float *) &p_max_ratio;
    out_params[P_MAX_RATIO].length = &ul_zero_value;
    out_params[P_MAX_RATIO].is_null = 0;
    
    // ratio
    float p_avg_ratio = 0;
    out_params[P_AVG_RATIO].buffer_type = MYSQL_TYPE_FLOAT;
    out_params[P_AVG_RATIO].buffer = (float *) &p_avg_ratio;
    out_params[P_AVG_RATIO].length = &ul_zero_value;
    out_params[P_AVG_RATIO].is_null = 0;

    // num_jobs
    long long p_num_jobs = 0;
    out_params[P_NJOBS].buffer_type = MYSQL_TYPE_LONGLONG;
    out_params[P_NJOBS].buffer = (long long *) &p_num_jobs;
    out_params[P_NJOBS].length = &ul_zero_value;
    out_params[P_NJOBS].is_null = 0;

    #ifdef DEBUG_MODE
    printf("\n\n *** COMPLETED ALL OUT_PARAMS STUFFS *** \n\n"); 
    printf("mysql_stmt_execute status: %d\n", status);
    printf("\nAfter mysql_execute and before mysql_stmt_bind_result\n\n");
    // printf("mem_lower_limit: %d\nmem_upper_limit: %d\n", p_lower_mem, p_upper_mem);
    #endif

    /* Fetch result set meta information */
    /*
     * prepare_meta_result = mysql_stmt_result_metadata(stmt);
    if (!prepare_meta_result)
    {
    	fprintf(stderr,
         " mysql_stmt_result_metadata(), \
           returned no meta information\n");
  	fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
  	exit(0);
    }
    */

    if ((status = mysql_stmt_bind_result(stmt, out_params)))
    {
        fprintf(stderr, "bind result failed: %d\n", status);
        exit(1);
    }

    /* Now buffer all results to client (optional step) */
    if (mysql_stmt_store_result(stmt))
    {
    	fprintf(stderr, " mysql_stmt_store_result() failed\n");
  	fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
  	exit(1);
    }


    int rows;
    int result;

    /*
     * #define MAX_USERNAME_LEN 16
     * #define MAX_JOBNAME_LEN 256
     * #define MAX_QUEUE_LEN 16
     * #define MAX_APP_LEN 20
     * #define MAX_SLA_LEN 50
     * #define MAX_PROJECT_LEN 16
    */

    // printf("username    jobname    MIN(maxRMem)    MIN(rusage_mem)    MIN(mem_ratio)    MAX(maxRMem)    MAX(rusage_mem)    MAX(mem_ratio)    num_jobs    queue    app    sla    project    \n\n");
    
    // sprintf(buffer, "| username%*.*s| jobname%*.*s| MIN(maxRMem)%*.*s| MIN(rusage_mem)|MIN(mem_ratio) %%| MAX(maxRMem)%*.*s| MAX(rusage_mem)|MAX(mem_ratio) %%| num_jobs%*.*s| queue%*.*s| app%*.*s| sla%*.*s| project%*.*s|\n", 7, 7, padding, 12, 12, padding, 3, 3, padding, 3, 3, padding, 7, 7, padding, 10, 10, padding, 16, 16, padding, 16, 16, padding, 8, 8, padding);

    sprintf(buffer, "| username%*.*s| queue%*.*s| app%*.*s| sla%*.*s| project%*.*s| jobname%*.*s| MIN(maxRMem) [MB]%*.*s| MAX(maxRMem) [MB]%*.*s| AVG(maxRMem) [MB]%*.*s| MIN(rusage_mem) [MB]| MAX(rusage_mem) [MB]| AVG(rusage_mem) [MB]| MIN(mem_ratio %%)%*.*s| MAX(mem_ratio %%)%*.*s| AVG(mem_ratio %%)%*.*s| num_jobs%*.*s|\n", 7, 7, padding, 10, 10, padding, 16, 16, padding, 16, 16, padding, 8, 8, padding, 248, 248, padding, 3, 3, padding, 3, 3, padding, 3, 3, padding, 4, 4, padding, 4, 4, padding, 4, 4, padding, 12, 12, padding);

    // sprintf(buffer, "| username%*.*s| jobname%*.*s| MIN(maxRMem) [MB]%*.*s| MIN(rusage_mem) [MB]| MIN(mem_ratio %%)%*.*s| MAX(maxRMem) [MB]%*.*s| MAX(rusage_mem) [MB]| MAX(mem_ratio %%)%*.*s| num_jobs%*.*s| queue%*.*s| app%*.*s| sla%*.*s| project%*.*s|\n", 7, 7, padding, 12, 12, padding, 3, 3, padding, 4, 4, padding, 3, 3, padding, 4, 4, padding, 12, 12, padding, 10, 10, padding, 16, 16, padding, 16, 16, padding, 8, 8, padding);

    sprintf(border_buffer, "+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+\n", MAX_USERNAME_LEN, MAX_USERNAME_LEN, border_padding, MAX_QUEUE_LEN, MAX_QUEUE_LEN, border_padding, MAX_APP_LEN, MAX_APP_LEN, border_padding, _MAX_SLA_LEN, _MAX_SLA_LEN, border_padding, MAX_PROJECT_LEN, MAX_PROJECT_LEN, border_padding, _MAX_JOBNAME_LEN, _MAX_JOBNAME_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding);


    // sprintf(border_buffer, "+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+\n", MAX_USERNAME_LEN, MAX_USERNAME_LEN, border_padding, _MAX_JOBNAME_LEN, _MAX_JOBNAME_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, MAX_QUEUE_LEN, MAX_QUEUE_LEN, border_padding, MAX_APP_LEN, MAX_APP_LEN, border_padding, _MAX_SLA_LEN, _MAX_SLA_LEN, border_padding, MAX_PROJECT_LEN, MAX_PROJECT_LEN, border_padding);
    printf("\nMEM Lower Bound: %d%%, MEM Upper Bound: %d%%;\nMIN Discriminant LCSS Jobname Length: %d;\nDATE Lower Bound: %s, DATE Upper Bound: %s.\n\n", p_lower_mem, p_upper_mem, p_min_discriminant_lcss_jobname_length, p_lower_date, p_upper_date);
    printf(border_buffer);
    printf(buffer);
    printf(border_buffer);

    // sprintf(mail_buffer, "<p style=\"background-color: black;\"><font color=#adff29>| </font><b><font color=\"red\">username</font></b><font color=#adff29>%*.*s| </font><b><font color=\"red\">jobname</font></b><font color=#adff29>%*.*s| </font><b><font color=\"red\">MIN(maxRMem)</font></b><font color=#adff29>%*.*s| </font><b><font color=\"red\">MIN(rusage_mem)</font></b><font color=#adff29>|</font><b><font color=\"red\">MIN(mem_ratio) %%</font></b><font color=#adff29>| </font><b><font color=\"red\">MAX(maxRMem)</font></b><font color=#adff29>%*.*s| </font><b><font color=\"red\">MAX(rusage_mem)</font></b><font color=#adff29>|</font><b><font color=\"red\">MAX(mem_ratio) %%</font></b><font color=#adff29>| </font><b><font color=\"red\">num_jobs</font></b><font color=#adff29>%*.*s| </font><b><font color=\"red\">queue</font></b><font color=#adff29>%*.*s| </font><b><font color=\"red\">app</font></b><font color=#adff29>%*.*s| </font><b><font color=\"red\">sla</font></b><font color=#adff29>%*.*s| </font><b><font color=\"red\">project</font></b><font color=#adff29>%*.*s|<br>", 7, 7, padding, 12, 12, padding, 3, 3, padding, 3, 3, padding, 7, 7, padding, 10, 10, padding, 16, 16, padding, 16, 16, padding, 8, 8, padding);
    //sprintf(mail_buffer, "%s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+<br>", mail_buffer, MAX_USERNAME_LEN, MAX_USERNAME_LEN, border_padding, _MAX_JOBNAME_LEN, _MAX_JOBNAME_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, MAX_QUEUE_LEN, MAX_QUEUE_LEN, border_padding, MAX_APP_LEN, MAX_APP_LEN, border_padding, _MAX_SLA_LEN, _MAX_SLA_LEN, border_padding, MAX_PROJECT_LEN, MAX_PROJECT_LEN, border_padding);

    // printf("\nMEM Lower Bound: %d, MEM Upper Bound: %d\nDATE Lower Bound: %s, DATE Upper Bound: %s\n\n", p_lower_mem, p_upper_mem, p_lower_date, p_upper_date);
    sprintf(mail_buffer, "<b>MEM Lower Bound</b>: %d%%, <b>MEM Upper Bound</b>: %d%%;<br><b>MIN Discriminant LCSS Jobname Length</b>: %d;<br><b>DATE Lower Bound</b>: %s, <b>DATE Upper Bound</b>: %s.<br><br>\n", p_lower_mem, p_upper_mem, p_min_discriminant_lcss_jobname_length, p_lower_date, p_upper_date);

    sprintf(mail_buffer, "%s<table style=\"background-color: black; color: #adff29;\"><tr style=\"color: red; font-weight: bold;\"><th>username</th><th>queue</th><th>app</th><th>sla</th><th>project</th><th>jobname</th><th>MIN(maxRMem) [MB]</th><th>MAX(maxRMem) [MB]</th><th>AVG(maxRMem) [MB]</th><th>MIN(rusage_mem) [MB]</th><th>MAX(rusage_mem) [MB]</th><th>AVG(rusage_mem) [MB]</th><th>MIN(mem_ratio %%)</th><th>MAX(mem_ratio %%)</th><th>AVG(mem_ratio %%)</th><th>num_jobs</th></tr>\n", mail_buffer);

    for(rows=0; !(result = mysql_stmt_fetch(stmt)); ++rows)
    {
        p_username[p_username_length] = '\0';
	// -- p_username_length;
	padLens[0] = MAX_USERNAME_LEN - p_username_length -1; 
        p_queue[p_queue_length] = '\0';
	// -- p_queue_length;
	padLens[1] = MAX_QUEUE_LEN - p_queue_length -1;
        p_app[p_app_length] = '\0';
	// -- p_app_length;
	padLens[2] = MAX_APP_LEN - p_app_length -1;
        p_sla[p_sla_length] = '\0';
	// -- p_sla_length;
	padLens[3] = _MAX_SLA_LEN - p_sla_length -1;

	if(padLens[3] <= 0)
                padLens[3] = 0;

	p_project[p_project_length] = '\0';
	//  -- p_project_length;
	padLens[4] = MAX_PROJECT_LEN - p_project_length -1;
        p_jobname[p_jobname_length] = '\0';
	// -- p_jobname_length;
	padLens[5] = _MAX_JOBNAME_LEN - p_jobname_length -1;

	if(padLens[5] <= 0)
                padLens[5] = 0;

                
	// printf("padLens[0]: %d\n", padLens[0]);

	sprintf(p_numbers[0], "%.2f ", p_min_maxrmem);
	numbersPadLens[0] = NUMBERS_FIXED_LEN - strlen(p_numbers[0]);
	sprintf(p_numbers[1], "%.2f ", p_max_maxrmem);
	numbersPadLens[1] = NUMBERS_FIXED_LEN - strlen(p_numbers[1]);
	sprintf(p_numbers[2], "%.2f ", p_avg_maxrmem);
	numbersPadLens[2] = NUMBERS_FIXED_LEN - strlen(p_numbers[2]);
	
	
	sprintf(p_numbers[3], "%.2f ", p_min_rusage);
	numbersPadLens[3] = NUMBERS_FIXED_LEN - strlen(p_numbers[3]);
	sprintf(p_numbers[4], "%.2f ", p_max_rusage);
	numbersPadLens[4] = NUMBERS_FIXED_LEN - strlen(p_numbers[4]);
	sprintf(p_numbers[5], "%.2f ", p_avg_rusage);
	numbersPadLens[5] = NUMBERS_FIXED_LEN - strlen(p_numbers[5]);	
	
	sprintf(p_numbers[6], "%.2f ", p_min_ratio);
	numbersPadLens[6] = NUMBERS_FIXED_LEN - strlen(p_numbers[6]);
	sprintf(p_numbers[7], "%.2f ", p_max_ratio);
	numbersPadLens[7] = NUMBERS_FIXED_LEN - strlen(p_numbers[7]);
	sprintf(p_numbers[8], "%.2f ", p_avg_ratio);
	numbersPadLens[8] = NUMBERS_FIXED_LEN - strlen(p_numbers[8]);
	
	sprintf(p_numbers[9], "%lld ", p_num_jobs);
	numbersPadLens[9] = NUMBERS_FIXED_LEN - strlen(p_numbers[9]);
	// printf("in result: %d\n", result);

	// printf("username length: %d, jobname_length: %d, queue_length: %d, app_length: %d, sla_length: %d, project_length: %d\n", p_username_length, p_jobname_length, p_queue_length, p_app_length, p_sla_length, p_project_length);

        // restore .19s to jobname placeholder if you want smarter jobname, but obviously less precise.
        printf("| %s%*.*s| %s%*.*s| %s%*.*s| %.19s%*.*s| %s%*.*s| %s%*.*s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s|\n", p_username, padLens[0], padLens[0], padding, p_queue, padLens[1], padLens[1], padding, p_app, padLens[2], padLens[2], padding, p_sla, padLens[3], padLens[3], padding, p_project, padLens[4], padLens[4], padding, p_jobname, padLens[5], padLens[5], padding, numbersPadLens[0], numbersPadLens[0], padding, p_numbers[0], numbersPadLens[1], numbersPadLens[1], padding, p_numbers[1], numbersPadLens[2], numbersPadLens[2], padding, p_numbers[2], numbersPadLens[3], numbersPadLens[3], padding, p_numbers[3], numbersPadLens[4], numbersPadLens[4], padding, p_numbers[4], numbersPadLens[5], numbersPadLens[5], padding, p_numbers[5], numbersPadLens[6], numbersPadLens[6], padding, p_numbers[6], numbersPadLens[7], numbersPadLens[7], padding, p_numbers[7], numbersPadLens[8], numbersPadLens[8], padding, p_numbers[8], numbersPadLens[9], numbersPadLens[9], padding, p_numbers[9]);
        
	//sprintf(mail_buffer, "%s| %s%*.*s| %s%*.*s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s|%*.*s%s| %s%*.*s| %s%*.*s| %s%*.*s| %s%*.*s|<br>", mail_buffer, p_username, padLens[0], padLens[0], padding, p_jobname, padLens[1], padLens[1], padding, numbersPadLens[0], numbersPadLens[0], padding, p_numbers[0], numbersPadLens[1], numbersPadLens[1], padding, p_numbers[1], numbersPadLens[2], numbersPadLens[2], padding,  p_numbers[2], numbersPadLens[3], numbersPadLens[3], padding, p_numbers[3], numbersPadLens[4], numbersPadLens[4], padding, p_numbers[4], numbersPadLens[5], numbersPadLens[5], padding,  p_numbers[5], numbersPadLens[6], numbersPadLens[6], padding, p_numbers[6], p_queue, padLens[2], padLens[2], padding, p_app, padLens[3], padLens[3], padding, p_sla, padLens[4], padLens[4], padding, p_project, padLens[5], padLens[5], padding);

	// sprintf(mail_buffer, "%s<tr><td>%s</td><td>%s</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%ld</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr><br>", mail_buffer, p_username_length ? p_username : " ", p_jobname_length ? p_jobname : " ", p_min_maxrmem, p_min_rusage, p_min_ratio, p_max_maxrmem, p_max_rusage, p_max_ratio, p_num_jobs, p_queue_length ? p_queue : " ", p_app_length ? p_app : " ", p_sla_length ? p_sla : " ", p_project_length ? p_project : " ");

	// sprintf(mail_buffer, "%s<tr><td>%s</td><td>%s</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%ld</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>", mail_buffer, p_username, p_jobname, p_min_maxrmem, p_min_rusage, p_min_ratio, p_max_maxrmem, p_max_rusage, p_max_ratio, p_num_jobs, p_queue, p_app, p_sla, p_project);

	// as previosly said, restore .19s to jobname placeholder if you want smarter jobname, but obviously less precise.
    	sprintf(mail_buffer, "%s<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%lld</td></tr>\n", mail_buffer, p_username, p_queue, p_app, p_sla, p_project, p_jobname, p_min_maxrmem, p_max_maxrmem, p_avg_maxrmem, p_min_rusage, p_max_rusage, p_avg_rusage, p_min_ratio, p_max_ratio, p_avg_ratio, p_num_jobs);

    }

    printf(border_buffer);
    // sprintf(mail_buffer, "%s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+%*.*s+<br><br>", mail_buffer, MAX_USERNAME_LEN, MAX_USERNAME_LEN, border_padding, _MAX_JOBNAME_LEN, _MAX_JOBNAME_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, NUMBERS_FIXED_LEN, NUMBERS_FIXED_LEN, border_padding, MAX_QUEUE_LEN, MAX_QUEUE_LEN, border_padding, MAX_APP_LEN, MAX_APP_LEN, border_padding, _MAX_SLA_LEN, _MAX_SLA_LEN, border_padding, MAX_PROJECT_LEN, MAX_PROJECT_LEN, border_padding);


    //printf("\n\n%s\n\n", mail_buffer);

    #ifdef DEBUG_MODE
    printf("mysql_stmt_fetch: %d\n", result);
    #endif

    printf("%d results returned\n", rows);
    sprintf(mail_buffer, "%s</table>\n<p>%d results returned.</p><br><br><br>", mail_buffer, rows);
   
    #ifdef DEBUG_MODE
    printf("mail_buffer length: %d\n", strlen(mail_buffer));
    #endif
    mysql_stmt_close(stmt);
    mysql_close(conn);

    
    if(is_mail_active)
        sendmail(from_mail, to_mail, mail_cmd_to, mail_buffer);
    

    #undef is_mail_active

    return 0;

}
