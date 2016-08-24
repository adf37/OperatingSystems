
import java.io.*;
import java.util.*;

public class vmsim {

    public static Queue requestQueue = new LinkedList();
    public static Queue diskQueue = new LinkedList();
    public static HashMap<String, ArrayList<Integer>> optMap = new HashMap<String, ArrayList<Integer>>();
    public static long startTime = System.currentTimeMillis();
    public static int ageWrites = 0;
    public static Hashtable ageDisk = new Hashtable();
    public static int numframes;

    public static void main(String[] args) throws IOException {
        if (!(args.length >= 3)) {
            System.err.print("Too few arguments entered on command line\nArguments required: <numframes>"
                    + "<opt|clock|aging|work> [<refresh>] [<tau>] <tracefile>");
            System.exit(-1);
        }
        numframes = Integer.parseInt(args[0]);
        String algorithm = args[1];
        String file = "";
        long refresh = 0, tau = 0;
        if (algorithm.equals("aging") || algorithm.equals("work")) {
            refresh = Integer.parseInt(args[2]);
            if (algorithm.equals("work")) {
                tau = Integer.parseInt(args[3]);
                file = args[4];
            } else {
                file = args[3];
            }
        } else {
            file = args[2];
        }
        Scanner infile = new Scanner(new File(file));
        while (infile.hasNext()) {
            String address = infile.next(); //hexadecimal address read in
            String mode = infile.next(); //R or W bit
            int bit = 0;
            if (mode.equals("W")) {
                bit = 1;
            }
            pageRequest temp = new pageRequest(address, mode, bit); //create new page request
            requestQueue.add(temp); //add to queue
        }
        if (algorithm.equals("opt")) {
            findOccurance(requestQueue);
            optimal(numframes);
        } else if (algorithm.equals("clock")) {
            clock(numframes);
        } else if (algorithm.equals("aging")) {
            aging(numframes, refresh);
        } else {
            workingSet(numframes, refresh, tau);
        }
    }

    public static void printResults(String alg, int nf, int ma, int pf, int w2d) {
        System.out.println(alg + "\nNumber of frames:\t" + nf + "\nTotal memory accesses:\t" + ma + "\nTotal page faults:\t"
                + pf + "\nTotal Writes to disk:\t" + w2d);
    }

    /*-------------------------------------------------------------------------------
     * Uses the optimal algorithm method to determine the proper paging to do based on knowing the future
     * Goes through a queue of all of the page requests and first checks to see if the current request is neither in the page frame already
     * and space is available. If space is not available must kick out page that is not used for the longest amount of time.
     * Takes in the total number of frames as a parameter
     * Returns nothing
     * Makes a call to the printResults method after completion
     ----------------------------------------------------------------------------------------*/
    public static void optimal(int numFrames) {
        int faults = 0, count = 0, writes = 0;
        Set<String> pF = new HashSet<String>();
        HashSet<String> disk = new HashSet<String>();
        while (requestQueue.size() > 0) { //while there are still requests
            boolean open = false;
            while (pF.size() < numFrames) { //make sure our page frame isn't full
                pageRequest temp = (pageRequest) requestQueue.poll(); //get head of the queue
                if (temp != null && !pF.contains(temp.va)) { //not in page frame then add it
                    pF.add(temp.va);
                    count++; //position counter
                    optMap.get(temp.va).remove(0); //remove next place(current spot) it occurs in the file
                    open = true;
                } else {
                    optMap.get(temp.va).remove(0); //remove next place(current spot) it occurs in the file
                    count++;
                }
            }
            if (open == false) { //nothing was open
                String toBeRemoved = ""; //index to be removed
                int maxdist = 0;
                pageRequest temp = (pageRequest) requestQueue.poll(); //get head of the queue
                if (pF.contains(temp.va)) { //if already in page frame
                    count++; //increment position
                    optMap.get(temp.va).remove(0); //remove occurance
                    continue;
                }
                for (String x : pF) { //go thru each frame to see which isn't used longest
                    int distance = 1;
                    ArrayList<Integer> l = new ArrayList<Integer>();
                    String current = x;
                    l.addAll(optMap.get(current)); //arraylist of all positions of address in file
                    if (l.isEmpty()) { //no further requests, get rid of it 
                        toBeRemoved = x;
                        break;
                    }
                    distance = l.get(0) - count; //get distance from current request to next occurance
                    if (distance >= maxdist) { //currently furthest away
                        maxdist = distance;
                        toBeRemoved = x; //set to be removed
                    }
                }
                if (temp != null) {
                    faults++; //had a page fault
                    if (temp.bit == 1) { //dirty page
                        if (!disk.contains(temp.va)) {
                            disk.add(temp.va);
                            writes++;
                        }
                        //writeDisk(temp);

                    }
                    pF.remove(toBeRemoved); //remove from page frame
                    pF.add(temp.va); //add new address
                    count++;
                    optMap.get(temp.va).remove(0);
                }
            }
        }

        printResults("opt", numFrames, 1000000, faults, writes);

    }

    /*---------------------------------------------------------------------------------
     * Finds each addresses occurances in the queue and records their position in a hashmap
     * Used in association with the optimal algorithm
     * Takes in the request queue of all 1000000 page requests and cycles through once
     * Returns nothing just adds to global hashmap
     ------------------------------------------------------------------------------------*/
    public static void findOccurance(Queue request) {
        //HashMap<String, Set<Integer>> map = new HashMap<String, Set<Integer>>();
        Queue temp = new LinkedList();
        temp.addAll(request); //add all elements of requestQueue to temporary queue 
        int index = 0;
        while (temp.size() > 0) {
            pageRequest p = (pageRequest) temp.poll(); //get head of queue
            if (optMap.containsKey(p.va)) { //if we already have seen this request
                ArrayList<Integer> s = optMap.get(p.va); //get its list of positions in file
                s.add(index); //add to list
                optMap.put(p.va, s); //add back to map
            } else { //unique address
                ArrayList<Integer> s = new ArrayList<Integer>();
                s.add(index); //add first occurance
                optMap.put(p.va, s); //add to map
            }
            index++;
        }

    }
    /*-----------------------------------------------------------------------------------
     * Implements the clock page replacement algorithm
     * Pointer points to most recently visited page in the set and checks if page is referenced or not
     * If referenced reset bit and give it a "second chance", if not swap page out for new request
     * Cycle through page frame until end of requests
     ------------------------------------------------------------------------------------*/

    public static void clock(int numFrames) {
        Hashtable disk = new Hashtable();
        int faults = 0, writes = 0, count = 0;
        pageRequest current;
        Hashtable table = new Hashtable(numFrames);
        Queue circQueue = new LinkedList(); //size of numFrames
        Queue tempQueue = new LinkedList();
        while (count < numFrames) { //while there are still empty spots just add to our queue of requests
            current = (pageRequest) requestQueue.poll();
            if (!table.containsKey(current.va)) {
                table.put(current.va, current.bit);
                count++;
            }
        }
        pageRequest request = (pageRequest) requestQueue.poll();
        circQueue.addAll(table.keySet());
        while (!requestQueue.isEmpty()) {
            boolean victim = false;
            tempQueue.addAll(circQueue);
            if (table.containsKey(request.va)) { //request is already in page frame
                table.put(request.va, 1); //referenced again so change bit
                //victim = true;
                if (requestQueue.isEmpty()) {
                    break;
                }
                request = (pageRequest) requestQueue.poll();
            }

            for (int i = 0; i < numFrames; i++) {
                String pointer = (String) tempQueue.poll();
                Iterator<Map.Entry<String, Integer>> iter = table.entrySet().iterator();
                Map.Entry<String, Integer> entry = iter.next();
                String curr = entry.getKey();
                int bit = entry.getValue();
                int ref = (int) table.get(pointer);
                if (!table.containsKey(request.va) && ref == 0) {
                    table.put(request.va, request.bit); //add in request
                    table.remove(pointer); //remove current pointer
                    circQueue.remove(pointer);
                    circQueue.add(request.va);
                    faults++;
                    if (requestQueue.isEmpty()) {
                        break;
                    }
                    request = (pageRequest) requestQueue.poll();
                    //break;
                    //victim = true; //found our page to evict
                } else if (ref == 1) { //cost of surviving
                    if (!disk.containsKey(pointer)) {
                        writes++;
                        disk.put(pointer, ref);
                    }

                    table.put(pointer, 0);

                }
            }
            //}
        }
        printResults("clock", numFrames, 1000000, faults, writes);
        long endTime = System.currentTimeMillis();
        System.out.println("Reading in file took: " + (endTime - startTime) / 1000.00 + " seconds");
    }
    /*-----------------------------------------------------------------------------------
     * Writes the given page to the disk because its reference bit is dirty
     * Takes in the pageRequest as a parameter
     * Returns nothing
     ------------------------------------------------------------------------------------*/

    public static void writeDisk(pageRequest page) {
        diskQueue.add(page);
    }

    /*-------------------------------------------------------------------------------------
     * Implements the aging page replacement scheme
     * 1. add in new page
     * 2. shift everything right 1 bit
     * 3. On every timer interrupt(refresh) the OS looks at each page
     * 4. Shft everything right 1 bit, if reference bit is set: set most-sig bit, then clear bit
     */
    public static void aging(int numframes, long refresh) {
        int faults = 0, writes = 0;
        pageRequest request;
        Hashtable requestTable = new Hashtable(numframes);
        Hashtable agingTable = new Hashtable(numframes);
        while (requestTable.size() < numframes) {
            request = (pageRequest) requestQueue.poll(); //get first request
            if (!requestTable.containsKey(request.va)) { //if hashtable doesn't contain request add
                requestTable.put(request.va, request.bit); //VA | reference bit
                agingTable.put(request.va, request.bit * (int) Math.pow(2, numframes)); //VA | reference counter intialized based on reference bit of reqeust
                //agingQueue.add(request.va);
            }
        }
        request = (pageRequest) requestQueue.poll();
        long start = System.currentTimeMillis();
        while (requestQueue.size() != 0) {
            //tempQueue.addAll(agingQueue);
            if (agingTable.containsKey(request.va)) {
                requestTable.put(request.va, 1);
                int counter = (int) agingTable.get(request.va);
                counter = ((int) Math.pow(2, numframes));
                agingTable.put(request.va, counter);
                if (requestQueue.size() == 0) {
                    break;
                }
                request = (pageRequest) requestQueue.poll();
            }
            for (int i = 0; i < numframes; i++) { //iterate over to put in new age values
                Iterator<Map.Entry<String, Integer>> iter = agingTable.entrySet().iterator();
                while (iter.hasNext()) {
                    Map.Entry<String, Integer> e = iter.next();
                    String pointer = e.getKey();
                    int c = e.getValue();
                    c = c / 2;
                    agingTable.put(pointer, c);

                }
                long current = (System.currentTimeMillis()) - start;
                if (current >= refresh) {
                    ageProcess(agingTable, requestTable);
                    //update queue of times
                    start = System.currentTimeMillis();
                }
                //String pointer = (String) tempQueue.poll();
                //int ref = (int) requestTable.get(pointer);
                if (!requestTable.containsKey(request.va)) { //have to check reference counter values
                    String removeKey = getMinAge(agingTable, requestTable);
                    requestTable.put(request.va, request.bit); //add in request
                    requestTable.remove(removeKey); //remove current pointer
                    agingTable.remove(removeKey);
                    agingTable.put(request.va, (int) Math.pow(2, numframes));
                    //agingQueue.remove(removeKey);
                    //agingQueue.add(request.va);
                    faults++;
                    if (requestQueue.size() == 0) {
                        break;
                    }
                    request = (pageRequest) requestQueue.poll();
                    //break;
                    //victim = true; //found our page to evict
                } else if (requestTable.containsKey(request.va)) {
                    requestTable.put(request.va, 1); //page was referenced update reference bit
                    if (requestQueue.size() == 0) {
                        break;
                    }
                    request = (pageRequest) requestQueue.poll();
                }
            }
        }

        printResults("aging", numframes, 1000000, faults, ageWrites);
        long endTime = System.currentTimeMillis() - startTime;
        System.out.println("Aging took: " + endTime / 1000.00 + " seconds");
    }

    /*-------------------------------------------------------------------
     * Ages the page queue for aging algorithm 
     * 1. shifts everything right 1 bit
     * 2. if reference bit is set then sets most sig bit then clears reference bit
     */
    public static void ageProcess(Hashtable agingTable, Hashtable requestTable) {
        Iterator<Map.Entry<String, Integer>> iter = agingTable.entrySet().iterator();
        while (iter.hasNext()) {
            Map.Entry<String, Integer> e = iter.next();
            String pointer = e.getKey();
            int c = e.getValue();
            c = c / 2; //shift bit right
            agingTable.put(pointer, c); //update table
            if (requestTable.get(pointer) == 1) { //most sig bit is set
                int count = (int) Math.pow(2, numframes);
                agingTable.put(pointer, (count + ((int) agingTable.get(pointer) / 2)));
                if (!ageDisk.containsKey(pointer)) {
                    ageWrites++;
                    ageDisk.put(pointer, 0);
                }

            }
            requestTable.put(pointer, 0); //clear reference bit
        }
    }

    /*-----------------------------------------------------------------------------
     * Searches through table of all the current pages and finds the page with minimum age that isn't referenced
     */
    public static String getMinAge(Hashtable agingTable, Hashtable requestTable) {
        String minKey = "";
        int minAge = 0;
        Iterator<Map.Entry<String, Integer>> iter = agingTable.entrySet().iterator();
        while (iter.hasNext()) {
            Map.Entry<String, Integer> entry = iter.next();
            String pointer = entry.getKey();
            int age = entry.getValue();
            if (minAge == 0 && requestTable.get(pointer) == 0) {
                minAge = age;
                minKey = pointer;
            } else {
                if (age < minAge && requestTable.get(pointer) == 0) {
                    minAge = age;
                    minKey = pointer;
                }
            }
        }
        return minKey;
    }

    /*----------------------------------------------------------------------------------------
     * All pages are kept in a circular list, as pages are added then advance clock hand
     * On page fault if reference bit is 0 and time of last use is not less than tau evict current pointed page
     * If reference bit is set then clear the reference bit and prepare to write to disk
     * else page is in the working set so move to next item in circular queue
     * 
     */
    public static void workingSet(int numframes, long refresh, long tau) {
        Queue workingSet = new LinkedList();
        Queue tempSet = new LinkedList();
        Hashtable requestTable = new Hashtable();
        Hashtable workingTable = new Hashtable();
        Hashtable disk = new Hashtable();
        pageRequest request;
        int count = 0, faults = 0, writes = 0;
        long start = System.currentTimeMillis();
        while (workingSet.size() < numframes) { //while there are still empty frames
            request = (pageRequest) requestQueue.poll();
            if (!workingTable.containsKey(request.va)) {
                requestTable.put(request.va, request.bit);
                //long currentTime = System.currentTimeMillis() - start;
                workingTable.put(request.va, count);
                workingSet.add(request.va);
            }
            count++;
        }
        request = (pageRequest) requestQueue.poll();
        //long start = System.currentTimeMillis();
        while (requestQueue.size() != 0) {
            tempSet.addAll(workingSet);
            if (requestTable.containsKey(request.va)) {
                requestTable.put(request.va, 1);
                workingTable.put(request.va, count);
                count++;
                if (requestQueue.size() == 0) {
                    break;
                }
                request = (pageRequest) requestQueue.poll();
            }

            for (int i = 0; i < numframes; i++) {
                boolean evicted = false;
                String pointer = (String) tempSet.poll();
                long currentTime = System.currentTimeMillis() - start;
                if (currentTime >= refresh) {
                    updateTime(requestTable, workingTable, count); //update time if refresh period passed
                    start = System.currentTimeMillis();
                }
                int ref = (int) requestTable.get(pointer);
                //not in table, reference bit is 0 and it is older than tau then evict
                if (!requestTable.containsKey(request.va) && ref == 0 && (int) workingTable.get(pointer) > tau) { //check to see if in WS
                    requestTable.put(request.va, request.bit); //add in request
                    requestTable.remove(pointer); //remove current pointer
                    workingTable.remove(pointer);
                    workingTable.put(request.va, count);
                    workingSet.remove(pointer);
                    workingSet.add(request.va);
                    faults++;
                    count++;
                    evicted = true;
                    if (requestQueue.isEmpty()) {
                        break;
                    }
                    request = (pageRequest) requestQueue.poll();
                    //break;
                    //victim = true; //found our page to evict

                } else if (ref == 1 && (int) workingTable.get(pointer) > tau) { //cost of surviving
                    if (!disk.containsKey(pointer)) {
                        disk.put(pointer, ref);
                        writes++;
                    }

                    requestTable.put(pointer, 0);

                } else if (requestTable.containsKey(request.va)) { //references a page already in set
                    requestTable.put(request.va, 1);
                    workingTable.put(request.va, count);
                    count++;
                    if (requestQueue.isEmpty()) {
                        break;
                    }
                    request = (pageRequest) requestQueue.poll();
                } else if (i == numframes - 1 && !evicted) { //reached end and no one was evicted
                    Iterator<Map.Entry<String, Integer>> iter = workingTable.entrySet().iterator();
                    int max = 0;
                    String remove = "";
                    while (iter.hasNext()) {
                        Map.Entry<String, Integer> entry = iter.next();
                        String point = entry.getKey();
                        int time = entry.getValue();
                        if (time > max) { //find oldest
                            max = time;
                            remove = point;
                        }
                    }
                    requestTable.remove(remove);
                    requestTable.put(request.va, request.bit);
                    workingTable.remove(remove);
                    workingTable.put(request.va, count);
                    workingSet.remove(remove);
                    workingSet.add(request.va);
                    count++;
                    faults++;
                    if (requestQueue.isEmpty()) {
                        break;
                    }
                    request = (pageRequest) requestQueue.poll();

                }
            }

        }
        printResults("workingSet", numframes, 1000000, faults, writes);
        long endTime = System.currentTimeMillis() - startTime;
        System.out.println("WorkingSet took: " + endTime / 1000.00 + " seconds");
    }
    //updates the time stamp of each page in in pageframes with current line number

    public static void updateTime(Hashtable requestTable, Hashtable workingTable, int counter) {
        Iterator<Map.Entry<String, Integer>> iter = requestTable.entrySet().iterator();
        while (iter.hasNext()) {
            Map.Entry<String, Integer> entry = iter.next();
            String key = entry.getKey();
            int vt = entry.getValue();
            requestTable.put(key, 0);
            workingTable.put(key, counter);
        }
    }

    public static class pageRequest {

        String va = new String();
        String mode = new String();
        int bit = 0;

        public pageRequest(String address, String mode, int bit) {
            va = address;
            this.mode = mode;
            this.bit = bit;
        }
    }
}
