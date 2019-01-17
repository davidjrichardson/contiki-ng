/**
 * TPWSN Trickle Simulation Script
 * 
 * Created by David Richardson, University of Warwick
 */

// TODO: Figure out a way to write the output to a singular file - the output could be multiple
// but that wouldn't be a very nice way to do things.
// TODO: Store the failed motes when they fail and remove them when they come back up
load("nashorn:mozilla_compat.js");
importPackage(java.io);

// The maximum number of nodes that can fail at once
var max_failure_count = 1;
// The recovery delay in clock ticks
var mote_recovery_delay = 500;

// Simulation file output object
var outputs = new Object();
// Simulation log file prefix
var file_prefix = "log_";

var all_motes = sim.getMotes();
var failed_motes = new Array(max_failure_count);

// The source and sink node IDs - they cannot be the same.
var source_id = Math.floor(Math.random() * all_motes.length);
var sink_id = Math.floor(Math.random() * all_motes.length);

// Make sure that the source and sink aren't the same
while (source_id === sink_id) {
   sink_id = Math.floor(Math.random() * all_motes.length);
}

// Actually get the mote(s) and tell them that they're these nodes
var source_node = all_motes[source_id];
var sink_node = all_motes[sink_id];

log.log("src: " + source_node + "\n");
log.log("snk: " + sink_node + "\n");

// Create a failable motes array
var failable_motes = new Array(all_motes.length - 2);
for (var i = 0; i < all_motes.length; i++) {
    if (i !== source_id && i !== sink_id) {
        failable_motes.push(all_motes[i]);
    }
}

// TODO: When a failure occurs:
// * Choose a set of nodes that are relevant to the failure mode
// --> Location based: Pick from 1-hop neighbourhood of failed node (or random if none)
// --> Temporal: Pick a node to fail at random
// * Figure out if the failure would break network constraint(s)
// --> Check if the neighbourhood of the node is going to partition
// --> Check if there are any more nodes that are allowed to fail
// * Choose node(s) to fail based on failure mode and number of nodes that are allowed to fail
// --> If the failure mode is temporal, another failure should happen shortly

GENERATE_MSG(2000, "start-sim");
YIELD_THEN_WAIT_UNTIL(msg.equals("start-sim"));

write(source_node, "set source");
write(source_node, "limit 1");
write(sink_node, "set sink");

TIMEOUT(10000, log.log("\n\nfoo\n"));

function fail_nodes(failure_mode) {
    if (failure_mode === "location") {
        // TODO: Pick a node to fail -- either 1 hop neighbour of current or a random node if not
    } else if (failure_mode === "temporal") {
        // TODO: Pick a note at random and schedule another failure (with increased likelihood)
    }
}

while (true) {
    // TODO: This is where the main sim loop sits
    // Need to update and track the failed mote(s), as well as write the mote output
    // to file(s)

    // Write the output for each mote to a file
    if (!outputs[id.toString()]) {
        log.log("Opening file for id " + id.toString() + "\n");
        outputs[id.toString()] = new FileWriter(file_prefix + id + ".txt");
    }
    
    outputs[id.toString()].write(time + ";" + msg + "\n");

    try {
        YIELD();
    } catch (e) {
        for (var ids in outputs) {
            log.log("Closing filewriter for id " + ids + "\n");
            outputs[ids].close();
        }

        throw("Simulation script killed")
    }
}
