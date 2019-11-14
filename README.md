# BOINC

Project Website: https://boinc.berkeley.edu

# BOINC-MGE

Mobile Grid Extension that add some aditional features to BOINC in terms of scheduling strategies.
Added a predictive model based scheduling strategy for mobile devices that uses the battery consuption rate as a factor for asign and deliver jobs.
Added a replication strategy that uses a RL like approach to calculate the number of replicas to generate for a given workunit based on the previous results and the battery consumed in other mobile devices.
Added a BOINC-MGE API that exposes some helper functions to integrate new scheduling strategies when running BOINC in a mobile grid enviroment.

## Want to create a BOINC project
See: https://boinc.berkeley.edu/trac/wiki

When using BOINC-MGE, some additional database tables have to be created. Be sure to run the SQL script db/schema_mge.sql in your project database if you want to enable BOINC-MGE features.

Also, be sure to compile all boinc components (including Android App) using the flag: --enable-boincmge

## Want to help translate
See: https://boinc.berkeley.edu/trac/wiki/TranslateIntro

## Want to contribute
See: https://boinc.berkeley.edu/trac/wiki/SoftwareDevelopment

### Note

The University of California holds the copyright on all BOINC source code. By 
submitting contributions to the BOINC code, you irrevocably assign all right, 
title, and interest, including copyright and all copyright rights, in such 
contributions to The Regents of the University of California, who may then 
use the code for any purpose that it desires. 

# License
BOINC is free software; you can redistribute it and/or modify it
under the terms of the GNU Lesser General Public License
as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

BOINC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with BOINC.  If not, see <https://www.gnu.org/licenses/>.

# BOINC-MGE Note
A paper will be published soon describing this extension and its results when used in a mobile grid environment.
