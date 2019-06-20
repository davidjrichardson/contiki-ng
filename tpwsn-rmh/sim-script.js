/**
 * TPWSN Rime Multihop Simulation Script
 *
 * Created by David Richardson, University of Warwick
 */
load("nashorn:mozilla_compat.js");
importPackage(java.io);
importPackage(java.util);
importPackage(org.contikios.cooja.util);
importPackage(org.contikios.cooja.interfaces);

DEBUG = false;
WRITE_OUTPUT = false;

/** Load the sim parameters from the filesystem
* maxFailureCount - The maximum number of nodes that can fail at once
* moteRecoveryDelay - The recovery delay in seconds
* moteFailureProbability - The failure probability for a single node (1/this value)
* simulationStopTick - The simulation tick to stop the simulation at (and start collecting data)
* failureMode - The mote failure mode for the sim
**/
load("params.js");

// Java types
var ArrayList = Java.type("java.util.ArrayList");
var HashMap = Java.type("java.util.HashMap");
var HashSet = Java.type("java.util.HashSet");
var ArrayDeque = Java.type("java.util.ArrayDeque");

// The random generator for failing motes (tied to the sim seed)
var rng = new Random(sim.getRandomSeed());
var terminating = false;

// Simulation file output object
var outputs = new Object();

var allMotes = sim.getMotes();
var failedMoteMap = new HashMap();

// Get the source mote for the RMH broadcast
var sourceMoteID = 1 + Math.floor(rng.nextFloat() * (allMotes.length - 1));
var sourceMote = allMotes[sourceMoteID];
log.log("src: " + sourceMote + "\n");

var sinkMoteID = 1
var sinkMote = allMotes[0];
log.log("snk: " + sinkMote + "\n");

// Create a failable motes array
var failableMotes = new ArrayList(allMotes.length - 2);
for (var i = 0; i < allMotes.length; i++) {
    if (i !== sourceMoteID && i !== sinkMoteID) {
        failableMotes.add(allMotes[i]);
    }
}

// Create the NodeGraph instance
var nodeGraph = new NodeGraph(sim);

// Initialise the outputs
if (WRITE_OUTPUT) {
    for each (var m in allMotes) outputs[m.getID().toString()] = new FileWriter(filePrefix + m.getID().toString() + ".txt");
}

var sourceNeighbours = new ArrayList();
var tokenMap = new HashMap(allMotes.length);
var consistentSet = new HashSet(allMotes.length);
var cumilativeCoverage = new ArrayList(allMotes.length);
cumilativeCoverage.add(sourceMote); // Add the source to the coverage list
var messagesSent = 0;
var announcements = 0;
var totalCrashes = 0;
var hasSentToken = false;

/**
 * A high-level function to fail node(s) in the simulation based on a specified failure mode
 * @param failureMode - The failure mode (String) to determine how to fail node(s).
 */
function failNode(failureMode) {
    // Return early if failureMode is "none" (control experiment)
    if (failureMode === "none") return;
    // Return early if the sim is terminating
    if (terminating) return;

    // Check if we can fail any more nodes and return early if not
    if ((maxFailureCount - failedMoteMap.size()) <= 0) {
        return;
    }

    // TODO: When a failure occurs:
    // * Choose a set of nodes that are relevant to the failure mode
    // --> Temporal: Pick a node to fail at random
    // * Figure out if the failure would break network constraint(s)
    // * Choose node(s) to fail based on failure mode and number of nodes that are allowed to fail
    // --> If the failure mode is temporal, another failure should happen shortly

    var moteToFail;
    if (failureMode === "location") {
        // Get a list of candidate motes to fail
        var moteSet = new HashSet(failableMotes.size());
        if (failedMoteMap.size() > 0) {
            // Get a list of all 1-hop neighbours for all of the failed motes
            // Duplicates don't matter since set operations later will clean them up
            failedMoteMap.keySet().forEach(function(elem) {
                moteSet.addAll(nodeGraph.get1HopNeighbours(elem));
            });
        } else {
            moteSet.addAll(failableMotes);
        }

        // Filter out any motes that have are already failed
        moteSet.removeAll(new HashSet(failedMoteMap.keySet()));

        // Cannot fail any more motes since the failable motes are all currently offline
        if (moteSet.isEmpty()) {
            return;
        }

        // Get a mote from the set
        var choice = rng.nextInt(moteSet.size());
        var moteSetList = new ArrayList(moteSet);
        moteToFail = moteSetList.get(choice);
    } else {
        moteToFail = failableMotes.get(rng.nextInt(failableMotes.size()));
    }

    // Check that the mote selected will not break sim constraints
    nodeGraph.toggleMote(moteToFail);
    if (!nodeGraph.isConnected()) {
        // Toggle it a 2nd time since the constraints are broken
        nodeGraph.toggleMote(moteToFail);
        return;
    }
    log.log("Failing mote " + moteToFail + " at time " + time + "\n");

    // Restart the mote
    var timeOfRestart = time + (1e6 * moteRecoveryDelay);
    failableMotes.remove(moteToFail);
    failedMoteMap.put(moteToFail, timeOfRestart);
    // log.log(failedMoteMap + "\n\n\n");
    if (cumilativeCoverage.contains(moteToFail)) {
        cumilativeCoverage.remove(moteToFail);
        log.log("Nodes covered: " + cumilativeCoverage.size() +" at time " + time + "\n");
    }
    write(moteToFail, "sleep " + moteRecoveryDelay);
    totalCrashes++;
}

/**
 * Function that is executed before the simulation ends. Output info about
 * simulation data (messages sent, coverage, total crashes)
 */
function endSimulation() {
    log.log("Messages sent: " + messagesSent + "\n");
    log.log("Announcements sent: " + announcements + "\n");
    log.log("Total crashes: " + totalCrashes + "\n");

    var tokenCorrect = 0;
    var incorrectTokenMotes = new ArrayList();

    // Add each mote to the incorrect list
    for each (var m in sim.getMotes()) {
        incorrectTokenMotes.add(m);
    }

    // Figure out how many motes have the correct token
    for each (var m in tokenMap.keySet()) {
        if (tokenMap.get(m).indexOf('hello') !== -1) {
            tokenCorrect++;
            incorrectTokenMotes.remove(m);
        }
    }
    log.log("Motes currently failed (" + failedMoteMap.size() + "): " + failedMoteMap + "\n");
    log.log("Motes reporting correctly: " + tokenCorrect + "\n");
    log.log("Motes reporting incorrectly: " + incorrectTokenMotes + "\n");
    log.log("Coverage: " + ((tokenCorrect*1.0/allMotes.length*1.0)*100) + "\n");

    throw("Simulation script killed")
}

while (true) {
    if (WRITE_OUTPUT) {
        // Write the mote output to a file
        outputs[mote.getID().toString()].write(time + ";" + msg + "\n");
    }

    // Figure out when its ok for the source to start the flood
    if (mote === sourceMote && msg.indexOf('adv_packet_received') > -1) {
        sourceNeighbours.add(msg);
    }

    if (sourceNeighbours.size() >= 3 && !hasSentToken) {
        log.log("Starting message flood on mote " + sourceMote + "\n");
        sourceMote.getInterfaces().getButton().clickButton();

        hasSentToken = true;
    }

    // Keep track of messages sent
    if (msg.indexOf('Forwarding packet to') > -1) {
        messagesSent++;
    }
    if (msg.indexOf('sending neighbor advertisement with val') > -1) {
        announcements++;
    }

    if (msg.indexOf('multihop message received') > -1 && !cumilativeCoverage.contains(mote)) {
        cumilativeCoverage.add(mote);
        log.log("Nodes covered: " + cumilativeCoverage.size() +" at time " + time + "\n");
    }
    
    try {
        YIELD();

        // Temp set to hold any motes removed from the failedMotemap
        var removeMotesFromMap = new HashSet(failedMoteMap.size());

        for each (var m in failedMoteMap.keySet()) {
            var restoreTime = failedMoteMap.get(m);

            // If the node needs to be brought back online
            if (time >= restoreTime) {
                nodeGraph.toggleMote(m);
                failableMotes.add(m);
                log.log("Mote " + m + " is online at time " + time + "\n");
                removeMotesFromMap.add(m);
            }
        }

        // Remove the motes from the failed mote map
        // Note: This is done this way to avoid iterator invalidation
        for each (var m in removeMotesFromMap) failedMoteMap.remove(m);
        removeMotesFromMap.clear();

        if (failureMode === "temporal") {
            // TODO
            // if (!failedMotes.isEmpty() && current) {
            //     if (random.nextInt(temporalCrashDelay) === 0) {
            //         failNode(failureMode);
            //     }
            // }
        } else {
            if (rng.nextInt(moteFailureProbability) === 0) {
                failNode(failureMode);
            }
        }

        // If there are no crashes, measure the time it takes to complete the sim
        if (maxFailureCount === 0) {
            // If the message is a consistency report then add it to the list of nodes with that message
            if (msg.indexOf('multihop message received \'hello\'') > -1 || msg.indexOf('sink received \'hello\'') > -1) {
                consistentSet.add(mote);
                tokenMap.putIfAbsent(mote, "hello");

                log.log(consistentSet + "\n");
                // log.log(mote + " reports hello (" + msg + ")\n");
            }

            // If all motes have reported consistency, report the time and end the sim
            if (msg.indexOf('sink received \'hello\'') > -1) {
                if (DEBUG) {
                    for (var ids in outputs) {
                        log.log("Closing filewriter for id " + ids + "\n");
                        outputs[ids].close();
                    }
                }

                consistentSet.add(mote);
                tokenMap.putIfAbsent(mote, "hello");
                log.log(consistentSet + "\n");
                log.log(time + " all motes converged, closing sim\n");

                endSimulation();
            }
        } else {
            // Otherwise, check if we are beyond the simulation stop tick to terminate the sim
            if (maxFailureCount > 0 && simulationStopTick > 0 && time >= simulationStopTick && terminating === false) {
                terminating = true;

                for each (var m in allMotes) write(m, "print");
            }
            
            if (msg.indexOf("Current token") !== -1) {
                consistentSet.add(mote);

                if (failedMoteMap.containsKey(mote)) {
                    tokenMap.put(mote, "NaN")
                } else {
                    if (msg.replace("Current token: ", "").length == 0) {
                        tokenMap.put(mote, "NaN");
                    } else {
                        tokenMap.put(mote, msg.replace("Current token: ", ""))
                    }
                }
            }

            if (consistentSet.size() === allMotes.length) {
                if (DEBUG) {
                    for (var ids in outputs) {
                        log.log("Closing filewriter for id " + ids + "\n");
                        outputs[ids].close();
                    }
                }

                log.log(time + " all online motes output token, closing sim\n");

                endSimulation();
            }
        }
    } catch (e) {
        // If the sim is supposed to cut short (with mote failures)
        if (DEBUG) {
            for (var ids in outputs) {
                log.log("Closing filewriter for id " + ids + "\n");
                outputs[ids].close();
            }
        }
        
        log.log(e);
        throw("Simulation script killed")
    }
}
