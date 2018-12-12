/**
 * TPWSN Trickle Simulation Script
 * 
 * Created by David Richardson, University of Warwick
 */

// TODO: Figure out a way to write the output to a singular file - the output could be multiple
// but that wouldn't be a very nice way to do things.
// TODO: Store the failed motes when they fail and remove them when they come back up

// importPackage(java.io);

// The maximum number of nodes that can fail at once
max_failure_count = 1
// The recovery delay in clock ticks
mote_recovery_delay = 500

// Simulation file output object
outputs = new Object();
// Simulation log file prefix
file_prefix = "log_"

all_motes = sim.getMotes();
failed_motes = new Array(max_failure_count);

// The source and sink node IDs - they cannot be the same.
source_id = Math.floor(Math.random() * all_motes.length);
sink_id = Math.floor(Math.random() * all_motes.length);

// Make sure that the source and sink aren't the same
while (source_id == sink_id) {
   sink_id = Math.floor(Math.random() * all_motes.length);
}

// Actually get the mote(s) and tell them that they're these nodes
source_node = all_motes[source_id];
sink_node = all_motes[sink_id];

log.log("src: " + source_node + "\n");
log.log("snk: " + sink_node + "\n");

// Create a failable motes array
failable_motes = new Array(all_motes.length - 2);
for (var i = 0; i < all_motes.length; i++) {
    if (i != source_id | i != sink_id) {
        failable_motes.push(all_motes[i]);
    }
}

log.log(failable_motes);

GENERATE_MSG(2000, "start-sim");
YIELD_THEN_WAIT_UNTIL(msg.equals("start-sim"));

write(source_node, "set source");
write(sink_node, "set sink");

TIMEOUT(10000, log.log("\n\nfoo\n"));

while (true) {
    // TODO: This is where the main sim loop sits
    // Need to update and track the failed mote(s), as well as write the mote output
    // to file(s)

    // Write the output for each mote to a file
    // if (!outputs[id.toString()]) {
    //     outputs[id.toString()] = new FileWriter(file_prefix + id + ".txt");
    // }
    // outputs[id.toString()].write(time + ";" + msg + "\n");

    try {
        YIELD();
    } catch (e) {
        // for (var ids in outputs) {
        //     outputs[ids].close();
        // }

        throw("Simulation script killed")
    }
}