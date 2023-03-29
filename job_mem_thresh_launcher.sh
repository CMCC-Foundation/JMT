#######################################
#     Job Memory Thresh (JMT) v1.0    #
#######################################
#                                     #
#         ██╗███╗   ███╗████████╗     #
#         ██║████╗ ████║╚══██╔══╝     #
#         ██║██╔████╔██║   ██║        #
#    ██   ██║██║╚██╔╝██║   ██║        #
#    ╚█████╔╝██║ ╚═╝ ██║   ██║        #
#     ╚════╝ ╚═╝     ╚═╝   ╚═╝        #
#                                     #
#######################################
# License: AGPL v3.
 # Giuseppe Calò                giuseppe.calo@cmcc.it
 # Danilo Mazzarella            danilo.mazzarella@cmcc.it 
 # Marco Chiarelli              marco.chiarelli@cmcc.it
 #                              marco_chiarelli@yahoo.it

#!/bin/bash

MEM_LOWER_LIMIT=${1:-"70"}
MEM_UPPER_LIMIT=${2:-"150"}
LOWER_DATE=${3-"2022-12-19"}
UPPER_DATE=${4-"2023-03-27"}
ACCT_SERVER=${5:-"127.0.0.1"}
ACCT_USER=${6:-"root"}
ACCT_PASSWORD="root"
ACCT_DATABASE="database"
MAIL_CMD="sendmail"
MAIL_FROM="scc-noreply@cmcc.it"
MAIL_TO="marco_chiarelli@yahoo.it"

#echo "MEM_LOWER_LIMIT: ""$MEM_LOWER_LIMIT"
#echo "MEM_UPPER_LIMIT: ""$MEM_UPPER_LIMIT"

./job_mem_thresh $MEM_LOWER_LIMIT $MEM_UPPER_LIMIT "$LOWER_DATE" "$UPPER_DATE" "$ACCT_SERVER" "$ACCT_USER" "$ACCT_PASSWORD" "$ACCT_DATABASE" "$MAIL_CMD" "$MAIL_FROM" "$MAIL_TO"
