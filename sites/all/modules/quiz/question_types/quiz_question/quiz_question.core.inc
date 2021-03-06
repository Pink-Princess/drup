<?php


/**
 * Classes used in the Quiz Question module.
 *
 * The core of the Quiz Question module is a set of abstract classes that
 * can be used to quickly and efficiently create new question types.
 *
 * Why OO?
 * Drupal has a long history of avoiding many of the traditional OO structures
 * and metaphors. However, with PHP 5, there are many good reasons to use OO
 * principles more broadly.
 *
 * The case for Quiz question types is that question types all share common
 * structure and logic. Using the standard hook-only Drupal metaphor, we are
 * forced to copy and paste large amounts of repetitive code from question
 * type to question type. By using OO principles and construction, we can
 * easily encapsulate much of that logic, while still making it easy to
 * extend the existing content.
 *
 * Where do I start?
 * To create a new question type, check out the multichoice question type for instance.
 *
 * @file
 */


/**
 * A base implementation of a quiz_question, adding a layer of abstraction between the
 * node API, quiz API and the question types.
 *
 * It is required that Question types extend this abstract class.
 *
 * This class has default behaviour that all question types must have. It also handles the node API, but
 * gives the question types oppurtunity to save, delete and provide data specific to the question types.
 *
 * This abstract class also declares several abstract functions forcing question-types to implement required
 * methods.
 */
abstract class QuizQuestion {
  /*
   * QUESTION IMPLEMENTATION FUNCTIONS
   *
   * This part acts as a contract(/interface) between the question-types and the rest of the system.
   *
   * Question types are made by extending these generic methods and abstract methods.
   */

  /**
   * The current node for this question.
   */
  public $node = NULL;

  // Extra node properties
  public $nodeProperties = NULL;

  /**
   * QuizQuestion constructor stores the node object.
   *
   * @param $node
   *   The node object
   */
  public function __construct(stdClass &$node) {
    $this->node = $node;
  }

  /**
   * Allow question types to override the body field title
   *
   * @return
   *  The title for the body field
   */
  public function getBodyFieldTitle() {
    return t('Question');
  }

  /**
   * Returns a node form to quiz_question_form
   *
   * Adds default form elements, and fetches question type specific elements from their
   * implementation of getCreationForm
   *
   * @param array $form_state
   * @return unknown_type
   */
  public function getNodeForm(array $form_state = NULL) {
    global $user;

    $form = array();

    // mark this form to be processed by quiz_form_alter. quiz_form_alter will among other things
    // hide the revion fieldset if the user don't have permission to controll the revisioning manually.
    $form['#quiz_check_revision_access'] = TRUE;

    $form['body_field']['body'] = array(
      '#type' => 'textarea',
      '#title' => $this->getBodyFieldTitle(),
      '#description' => t('Enter the full text of the question that will be shown to the user. Include any special instructions on how to answer.'),
      '#default_value' => $this->node->body,
      '#required' => TRUE,
      '#weight' => -15,
    );

    // process up the filter format
    $form['body_field']['format'] = filter_form($this->node->format);
    $form['body_field']['format']['#weight'] = -14;
    $form['body_field']['#weight'] = -15;

    // Allow user to set title?
    if (user_access('edit question titles')) {
      $form['helper']['#theme'] = 'quiz_question_creation_form';
      $form['title'] = array(
        '#type' => 'textfield',
        '#title' => t('Title'),
        '#default_value' => $this->node->title,
        '#required' => FALSE,
        '#description' => t('Add a title that will help distinguish this question from other questions. This will not be seen during the quiz.'),
      );
    }
    else {
      $form['title'] = array(
        '#type' => 'value',
        '#value' => $this->node->title,
      );
    }

    // Quiz id used here to tie creation of a question to a specific quiz
    if (isset($_GET['quiz_nid']) && is_numeric($_GET['quiz_nid'])) {
      $vid = (_quiz_is_int($_GET['quiz_vid'], 0)) ? $_GET['quiz_vid'] : NULL;
      $quiz = node_load((int)$_GET['quiz_nid'], $vid);

      // Store quiz id in the form
      $form['quiz_nid'] = array(
        '#type' => 'value',
        '#value' => $quiz->nid,
      );
      $form['quiz_vid'] = array(
        '#type' => 'value',
        '#value' => $quiz->vid,
      );

      // Identify this node as a quiz question type so that it can be recognized by other modules effectively.
      $form['is_quiz_question'] = array(
        '#type' => 'value',
        '#value' => TRUE
      );

      // If coming from quiz view, go back there on submit.
      if ($quiz->type == 'quiz') {
        $form['#redirect'] = 'node/'. $quiz->nid .'/questions';
        $form['#cancel_button'] = TRUE;
      }
    }

    //Add question type specific content
    $form = array_merge($form, $this->getCreationForm($form_state));

    // If access to edit quizzes we add the add to quiz fieldset
    $edit_access = quiz_access_multi_or('edit any quiz', 'edit own quiz', 'administer nodes');
    if ($edit_access) {
      $own_filter = quiz_access_multi_or('edit any quiz', 'administer nodes') ? '' : 'AND n.uid = '. intval($user->uid);

      // Fieldset allowing question makers to add questions to multiple quizzes when creating or editing a question
      $already = array();
      $already_nids = array();
      if (isset($this->node->nid) && is_numeric($this->node->nid)) {
        // Finding quizzes this question already belongs to.
        $sql = "SELECT n.nid, r.parent_vid AS vid, n.title
                FROM {quiz_node_relationship} r
                JOIN {node} n
                ON n.nid = r.parent_nid
                WHERE r.child_vid = %d $own_filter
                ORDER BY r.parent_vid DESC";
        $res = db_query(db_rewrite_sql($sql), $this->node->vid);

        // Store the results
        while ($res_o = db_fetch_object($res)) {
          if (in_array($res_o->nid, $already_nids)) continue;
          // Store in simple array to use in later querries
          $already_nids[] = $res_o->nid;

          // Store in array to use as #options
          $already[$res_o->nid .'-'. $res_o->vid] = check_plain($res_o->title);
        }
      }
      $found = implode(', ', $already_nids);
      $latest = array();
      $latest_nids = array();

      // Finding the last quizzes the current user has been using
      $sql = "SELECT lq.quiz_nid, n.vid, n.title, lq.id
              FROM {quiz_question_latest_quizzes} lq
              JOIN {node} n
              ON n.nid = lq.quiz_nid
              JOIN {quiz_node_properties} qnp
              ON qnp.vid = n.vid
              WHERE lq.uid = %d AND qnp.randomization < 3 $own_filter";
      if (drupal_strlen($found) > 0) $sql .= " AND quiz_nid NOT IN ($found)";
      $sql .= " ORDER BY lq.id DESC";
      $res = db_query(db_rewrite_sql($sql), $user->uid);

      while ($res_o = db_fetch_object($res)) {
        // Array to use as #option in form element
        $latest[$res_o->quiz_nid .'-'. $res_o->vid] = check_plain($res_o->title);
        // Array to use in later queries
        $latest_nids[] = $res_o->quiz_nid;
      }

      if (count($latest) < QUIZ_QUESTION_NUM_LATEST) {
        // Suplementing with other available quizzes...
        $found = implode(', ', array_merge($already_nids, $latest_nids));
        $sql = "SELECT n.nid, n.vid, title, changed
                FROM {node} n
                JOIN {quiz_node_properties} qnp
                ON qnp.vid = n.vid
                WHERE type = 'quiz' AND qnp.randomization < 3 $own_filter";
        if (drupal_strlen($found) > 0) {
          $sql .= " AND n.nid NOT IN ($found)";
        }
        $sql .= " ORDER BY changed LIMIT %d";
        $res = db_query(db_rewrite_sql($sql), QUIZ_QUESTION_NUM_LATEST - count($latest));
        while ($res_o = db_fetch_object($res)) {
          // array to be used as #options in form element
          $latest[$res_o->nid .'-'. $res_o->vid] = check_plain($res_o->title);
        }
      }

      // If we came from the manage questions tab we need to mark the quiz we came from as selected.
      $latest_default = array();
      if (isset($quiz)) {
        foreach ($latest as $key => $value) {
          $latest_nid = preg_match('/^[0-9]+/', $key);
          if ($latest_nid == $quiz->nid) {
            unset($latest[$key]);
            break;
          }
        }
        $sql = 'SELECT title
                FROM {node_revisions}
                WHERE vid = %d';
        $res = db_query($sql, $quiz->vid);
        $latest[$quiz->nid .'-'. $quiz->vid] = check_plain(db_result($res));
        // $latest_default is to be used as #default_value in form item
        $latest_default[] = $quiz->nid .'-'. $quiz->vid;
      }
    }
    if ($edit_access || quiz_access_multi_or('create quiz', 'administer nodes')) {
      $form['add_directly'] = array(
        '#type' => 'fieldset',
        '#title' => t('Add to @quiz', array('@quiz' => QUIZ_NAME)),
        '#collapsible' => TRUE,
        '#collapsed' => TRUE,
        '#weight' => -3,
        '#tree' => TRUE,
      );
    }
    if ($edit_access) {
      if (count($already) > 0) {
        $form['add_directly']['already'] = array(
          '#type' => 'checkboxes',
          '#title' => t('This question is already a member of'),
          '#description' => t('If you uncheck any of the checkboxes this question will be removed from the corresponding @quiz. If the @quiz has been answered a new revision of the @quiz will be created automatically.', array('@quiz' => QUIZ_NAME)),
          '#options' => $already,
          '#default_value' => array_keys($already),
        );
      }
      if (count($latest) > 0) {
        $form['add_directly']['latest'] = array(
          '#type' => 'checkboxes',
          '#title' => t('The @latest latest @quiz nodes this question isn\'t a member of', array('@latest' => count($latest), '@quiz' => QUIZ_NAME)),
          '#description' => t('If you check any of the checkboxes this question will be added to the corresponding @quiz. If the @quiz has been answered a new revision will be created automatically.', array('@quiz' => QUIZ_NAME)),
          '#options' => $latest,
          '#default_value' => $latest_default,
        );
      }
    }
    if (quiz_access_multi_or('create quiz', 'administer nodes')) {
      $form['add_directly']['new'] = array(
        '#type' => 'textfield',
        '#title' => t('Title for new @quiz', array('@quiz' => QUIZ_NAME)),
        '#description' => t('Write in the name of the new @quiz you want to create and add this question to.', array('@quiz' => QUIZ_NAME)),
      );
    }
    if ($this->hasBeenAnswered()) {
      $log = t('The current revision has been answered. We create a new revision so that the reports from the existing answers stays correct.');
      $this->node->revision = 1;
      $this->node->log = $log;
    }
    return $form;
  }

  /**
   * Retrieve information relevant for viewing the node.
   *
   * (This data is generally added to the node's extra field.)
   *
   * @return
   *  Content array
   */
  public function getNodeView() {
    $type = node_get_types('type', $this->node);
    $content['question_type'] = array(
      '#type'  => 'markup',
      '#value' => '<div class="question_type_name">' . $type->name . '</div>',
      '#weight' => -2,
    );
    $content['question'] = array(
      '#type'  => 'markup',
      '#value' => '<div class="question-body">' . check_markup($this->node->body, $this->node->format, FALSE) . '</div>',
      '#weight' => -1,
    );
    return $content;
  }

  /**
   * Getter function returning properties to be loaded when the node is loaded.
   *
   * @see load hook in quiz_question.module (quiz_question_load)
   *
   * @return array
   */
  public function getNodeProperties() {
    if (isset($this->nodeProperties)) return $this->nodeProperties;
    $sql = 'SELECT max_score
            FROM {quiz_question_properties}
            WHERE nid = %d AND vid = %d';
    $props['max_score'] = db_result(db_query($sql, $this->node->nid, $this->node->vid));
    $props['is_quiz_question'] = TRUE;
    $this->nodeProperties = $props;
    return $props;
  }

  /**
   * Responsible for handling insert/update of question-specific data.
   * This is typically called from within the Node API, so there is no need
   * to save the node.
   *
   * The $is_new flag is set to TRUE whenever the node is being initially
   * created.
   *
   * A save function is required to handle the following three situations:
   * - A new node is created ($is_new is TRUE)
   * - A new node *revision* is created ($is_new is NOT set, because the
   *   node itself is not new).
   * - An existing node revision is modified.
   *
   * @see hook_update and hook_insert in quiz_question.module
   *
   * @param $is_new
   *  TRUE when the node is initially created.
   */
  public function save($is_new = FALSE) {
    // We call the abstract function saveNodeProperties to save type specific data
    $this->saveNodeProperties($is_new);

    $is_new_node = $is_new || $this->node->revision == 1;

    // Save general data
    if ($is_new_node) {
      $sql = 'INSERT INTO {quiz_question_properties}
              (nid, vid, max_score)
              VALUES(%d, %d, %d)';
      db_query($sql, $this->node->nid, $this->node->vid, $this->getMaximumScore());
    }
    else {
      $sql = 'UPDATE {quiz_question_properties}
              SET max_score = %d
              WHERE nid = %d AND vid = %d';
      db_query($sql, $this->getMaximumScore(), $this->node->nid, $this->node->vid);
    }

    // Save what quizzes this question belongs to.
    $quizzes_kept = $this->saveRelationships();
    if ($quizzes_kept && $this->node->revision) {
      if (user_access('manual quiz revisioning') && !variable_get('quiz_auto_revisioning', 1)) {
        unset($_REQUEST['destination']);
        unset($_REQUEST['edit']['destination']);
        drupal_goto('quiz_question/'. $this->node->nid .'/'. $this->node->vid .'/revision_actions');
      }
      // For users without the 'manual quiz revisioning' permission we submit the revision_actions form
      // silently with its default values set.
      else {
        $form_state = array();
        $form_state['values']['op'] = t('Submit');
        require_once(drupal_get_path('module', 'quiz_question') .'/quiz_question.pages.inc');
        drupal_execute('quiz_question_revision_actions', $form_state, $this->node->nid, $this->node->vid);
      }
    }
  }

  /**
   * Delete question data from the database.
   *
   * Called by quiz_question_delete (hook_delete).
   * Child classes must call super
   *
   * @param $only_this_version
   *  If the $only_this_version flag is TRUE, then only the particular
   *  nid/vid combo should be deleted. Otherwise, all questions with the
   *  current nid can be deleted.
   */
  public function delete($only_this_version = FALSE) {
    // Delete results
    db_query('DELETE FROM {quiz_node_results_answers}
              WHERE question_nid = %d' . ($only_this_version ? ' AND question_vid = %d' : ''),
              $this->node->nid, $this->node->vid);

    // Delete properties
    $sql = 'DELETE FROM {quiz_question_properties}
            WHERE nid = %d';
    if ($only_this_version) $sql .= ' AND vid = %d';
    db_query($sql, $this->node->nid, $this->node->vid);
  }

  /**
   * Provides validation for question before it is created.
   *
   * When a new question is created and initially submited, this is
   * called to validate that the settings are acceptible.
   *
   * @param $form
   *  The processed form.
   */
  abstract public function validateNode(array &$form);

  /**
   * Get the form through which the user will answer the question.
   *
   * @param $form_state
   *  The FAPI form_state array
   * @param $rid
   *  The result id.
   * @return
   *  Must return a FAPI array. At the moment all form elements that takes
   *  user response must have a key named "tries". (This is a Quiz 3.x legacy AFAIK. I'm
   *  not thrilled about it...)
   */
  public function getAnsweringForm(array $form_state = NULL, $rid) {
    $form = array();
    $form['question_nid'] = array(
      '#type' => 'hidden',
      '#value' => $this->node->nid,
    );

    $form['question'] = array(
      '#type' => 'markup',
      '#value' => check_markup($this->node->body, $this->node->format, FALSE),
      '#prefix' => '<div class="quiz-question-body">',
      '#suffix' => '</div>',
    );

    return $form;
  }

  /**
   * Get the form used to create a new question.
   *
   * @param
   *  FAPI form state
   * @return
   *  Must return a FAPI array.
   */
  abstract public function getCreationForm(array $form_state = NULL);

  /**
   * Get the maximum possible score for this question.
   */
  abstract public function getMaximumScore();

  /**
   * Save question type specific node properties
   */
  abstract public function saveNodeProperties($is_new = FALSE);

  /**
   * Handle the add to quiz part of the quiz_question_form
   *
   * @return
   *  TRUE if at least one of the questions were kept.
   *  FALSE otherwise
   */
  function saveRelationships() {
    $quizzes_kept = FALSE;
    unset($_SESSION['quiz_question_kept']);
    /*
     * If the question already is part of quizzes we might have to remove some
     * relationships
     */
    $quizzes_to_update = array();
    if (isset($this->node->add_directly['already']) && is_array($this->node->add_directly['already'])) {
      foreach ($this->node->add_directly['already'] as $key => $checked) {
        if ($checked == 0) {
          $nid_vid = explode('-', $key);
          $dummy_node = new stdClass();
          $dummy_node->nid = $nid_vid[0];
          $dummy_node->vid = $nid_vid[1];
          if (quiz_has_been_answered($dummy_node)) {
            // We need to revise the quiz node if it has been answered
            $temp_quiz_node = node_load($dummy_node->nid, $dummy_node->vid);
            $temp_quiz_node->revision = 1;
            $temp_quiz_node->auto_created = TRUE;
            node_save($temp_quiz_node);
            $nid_vid[1] = $temp_quiz_node->vid;
            drupal_set_message(t('New revision has been created for the @quiz %n', array('%n' => $temp_quiz_node->title, '@quiz' => QUIZ_NAME)));
          }
          $quizzes_to_update[] = $nid_vid[1];
          $sql = 'DELETE FROM {quiz_node_relationship}
                  WHERE parent_nid = %d AND parent_vid = %d AND child_nid = %d AND child_vid = %d';
          db_query($sql, $nid_vid[0], $nid_vid[1], $this->node->nid, $this->node->vid);
        }
        else {
          $quizzes_kept = TRUE;
          $_SESSION['quiz_question_kept'][] = $key;
        }
      }
    }

    /*
     * The quiz question might have been added to new quizzes
     */
    if (is_array($this->node->add_directly['latest'])) {
      $to_insert = 'VALUES';
      $insert_values = array();
      foreach ($this->node->add_directly['latest'] as $nid => $checked) {
        if ($checked != 0) {
          $nid_vid = explode('-', $checked);
          $dummy_node = new stdClass();
          $dummy_node->nid = $nid_vid[0];
          $dummy_node->vid = $nid_vid[1];
          if (quiz_has_been_answered($dummy_node)) {
            $temp_quiz_node = node_load($dummy_node->nid, $dummy_node->vid);
            $temp_quiz_node->revision = 1;
            $temp_quiz_node->auto_created = TRUE;
            node_save($temp_quiz_node);
            $nid_vid[1] = $temp_quiz_node->vid;
            drupal_set_message(t('New revision has been created for the @quiz %n', array('%n' => $temp_quiz_node->title, '@quiz' => QUIZ_NAME)));
          }
          $quizzes_to_update[] = $nid_vid[1];
          // We build one sql query to insert multiple rows into the relationship table
          if (count($insert_values) > 0) $to_insert .= ',';
          $to_insert .= ' (%d, %d, %d, %d, %d, %d, %d)';
          $insert_values[] = $nid_vid[0];
          $insert_values[] = $nid_vid[1];
          $insert_values[] = $this->node->nid;
          $insert_values[] = $this->node->vid;
          $insert_values[] = $this->getMaximumScore();
          $insert_values[] = 1 + db_result(
            db_query('
              SELECT MAX(weight)
              FROM {quiz_node_relationship}
              WHERE parent_vid = %d
            ', $nid_vid[1]));
          $randomization = db_result(
            db_query('
              SELECT randomization
              FROM {quiz_node_properties}
              WHERE nid = %d AND vid = %d
            ', $nid_vid[0], $nid_vid[1]));
          $insert_values[] = $randomization == 2 ? QUESTION_RANDOM : QUESTION_ALWAYS;

          // We also delete multiple rows from the table...
          $delete_values[] = $nid_vid[1];
          $delete_values[] = $this->node->nid;
          $delete_sqls[] = '(parent_vid = %d AND child_nid = %d)';
        }
      }
      if (count($insert_values) > 0) {
        $sql = 'DELETE FROM {quiz_node_relationship}
                WHERE '. implode(' OR ', $delete_sqls);
        db_query($sql, $delete_values);
        $sql = 'INSERT INTO {quiz_node_relationship}
                (parent_nid, parent_vid, child_nid, child_vid, max_score, weight, question_status)
                '. $to_insert;
        db_query($sql, $insert_values);
      }
    }
    if (drupal_strlen($this->node->add_directly['new']) > 0) {
      $new_node = quiz_make_new($this->node->add_directly['new']);
      $sql = 'INSERT INTO {quiz_node_relationship}
              (parent_nid, parent_vid, child_nid, child_vid, max_score)
              VALUES (%d, %d, %d, %d, %d)';
      db_query($sql, $new_node->nid, $new_node->vid, $this->node->nid, $this->node->vid, $this->getMaximumScore());
      $quizzes_to_update[] = $new_node->vid;
    }
    quiz_update_max_score_properties($quizzes_to_update);
    return $quizzes_kept;
  }

  /**
   * Finds out if a question has been answered or not
   *
   * This function also returns TRUE if a quiz that this question belongs to have been answered.
   * Even if the question itself haven't been answered. This is because the question might have
   * been rendered and a user is about to answer it...
   *
   * @return
   *   true if question has been answered or is about to be answered...
   */
  public function hasBeenAnswered() {
    if (!isset($this->node->vid)) return FALSE;
    $sql = 'SELECT *
            FROM {quiz_node_results}
            WHERE vid IN (
              SELECT parent_vid
              FROM {quiz_node_relationship}
              WHERE child_vid = %d
            )
            LIMIT 1';
    $res = db_query($sql, $this->node->vid);
    return db_fetch_object($res) ? TRUE : FALSE;
  }

  /**
   * Determines if the user can view the correct answers
   *
   * @return boolean
   *   true iff the view may include the correct answers to the question
   */
  public function viewCanRevealCorrect() {
    global $user;
    // permission overrides the hook
    if (user_access('view any quiz question correct response')
    || $user->uid == $this->node->uid)
      return TRUE;
    $results = module_invoke_all('answers_access', $node);
    $may_view_answers = in_array(TRUE, $results);
    return $may_view_answers;
  }
}

/**
 * Each question type must store its own response data and be able to calculate a score for
 * that data.
 */
abstract class QuizQuestionResponse {
  // Result id
  protected $rid = 0;

  protected $is_correct = FALSE;
  protected $evaluated = TRUE;

  // The question node(not a quiz question instance)
  public $question = NULL;

  protected $answer = NULL;
  protected $score;
  public $is_skipped;

  /**
   * Create a new user response.
   *
   * @param $result_id
   *  The result ID for the user's result set. There is one result ID per time
   *  the user takes a quiz.
   * @param $question_node
   *  The question node.
   * @param $answer
   *  The answer (dependent on question type).
   */
  public function __construct($result_id, stdClass $question_node, $answer = NULL) {
    $this->rid = $result_id;
    $this->question = $question_node;
    $this->answer = $answer;
    $sql = 'SELECT is_skipped
            FROM {quiz_node_results_answers}
            WHERE result_id = %d AND question_nid = %d AND question_vid = %d';
    $res = db_query($sql, $result_id, $question_node->nid, $question_node->vid);
    $this->is_skipped = db_result($res);
  }

  /**
   * Used to refresh this instances question node in case drupal has changed it.
   *
   * @param $newNode
   *  Question node
   */
  public function refreshQuestionNode($newNode) {
    $this->question = $newNode;
  }

  /**
   * Indicate whether the response has been evaluated (scored) yet.
   * Questions that require human scoring (e.g. essays) may need to manually
   * toggle this.
   */
  public function isEvaluated() {
    return (bool)$this->evaluated;
  }

  /**
   * Check to see if the answer is marked as correct.
   *
   * This default version returns TRUE iff the score is equal to the maximum possible score.
   */
  function isCorrect() {
    $possible = $this->getMaxScore();
    $actual = $this->getScore();

    return ($possible == $actual);
  }

  /**
   * Returns stored score if it exists, if not the score is calculated and returned.
   *
   * @param $weight_adjusted
   *  If the returned score shall be adjusted according to the max_score the question has in a quiz
   * @return
   *  Score(int)
   */
  function getScore($weight_adjusted = TRUE) {
    if ($this->is_skipped) return 0;
    if (!isset($this->score)) $this->score = $this->score();
    if (isset($this->question->score_weight) && $weight_adjusted)
      return round($this->score * $this->question->score_weight);
    return $this->score;
  }

  /**
   * Returns stored max score if it exists, if not the max score is calculated and returned.
   *
   * @param $weight_adjusted
   *  If the returned max score shall be adjusted according to the max_score the question has in a quiz
   * @return
   *  Max score(int)
   */
  public function getMaxScore($weight_adjusted = TRUE) {
    if (!isset($this->question->max_score)) $this->question->max_score = $this->question->getMaximumScore();
    if (isset($this->question->score_weight) && $weight_adjusted)
      return round($this->question->max_score * $this->question->score_weight);
    return $this->question->max_score;
  }

  /**
   * Represent the response as a stdClass object.
   *
   * Convert data to an object that has the following properties:
   * - $score
   * - $rid
   * - $nid
   * - $vid
   * - $is_correct
   */
  function toBareObject() {
    $obj = new stdClass();
    $obj->score = $this->getScore(); // This can be 0 for unscored.
    $obj->nid = $this->question->nid;
    $obj->vid = $this->question->vid;
    $obj->rid = $this->rid;
    $obj->is_correct = $this->isCorrect();
    $obj->is_evaluated = $this->isEvaluated();
    $obj->is_skipped = FALSE;
    $obj->is_valid = $this->isValid();
    return $obj;
  }

  /**
   * Validates response from a quiz taker. If the response isn't valid the quiz taker won't be allowed to proceed.
   *
   * @return
   *  True if the response is valid.
   *  False otherwise
   */
  public function isValid() {
    return TRUE;
  }

  /**
   * Get data suitable for reporting a user's score on the question.
   * This expects an object with the following attributes:
   *
   *  answer_id; // The answer ID
   *  answer; // The full text of the answer
   *  is_evaluated; // 0 if the question has not been evaluated, 1 if it has
   *  score; // The score the evaluator gave the user; this should be 0 if is_evaluated is 0.
   *  question_vid
   *  question_nid
   *  result_id
   */
  public function getReport() {
    // Basically, we encode internal information in a
    // legacy array format for Quiz.
    $report = array(
      'answer_id' => 0, // <-- Stupid vestige of multichoice.
      'answer' => $this->answer,
      'is_evaluated' => $this->isEvaluated(),
      'is_correct' => $this->isCorrect(),
      'score' => $this->getScore(),
      'question_vid' => $this->question->vid,
      'question_nid' => $this->question->nid,
      'result_id' => $this->rid,
    );

    return $report;
  }

  /**
   * Creates the report form for the admin pages, and for when a user gets feedback after answering questions.
   *
   * The report is a form to allow editing scores and the likes while viewing the report form
   *
   * @param $showpoints
   * @param $showfeedback
   * @param $allow_scoring
   * @return $form
   *  Drupal form array
   */
  public function getReportForm($showpoints = TRUE, $showfeedback = TRUE, $allow_scoring = FALSE) {
    /*
     * Add general data, and data from the question type implementation
     */
    $form = array();
    $form['nid'] = array(
      '#type' => 'value',
      '#value' => $this->question->nid,
    );
    $form['vid'] = array(
      '#type' => 'value',
      '#value' => $this->question->vid,
    );
    $form['rid'] = array(
      '#type' => 'value',
      '#value' => $this->rid,
    );
    if ($submit = $this->getReportFormSubmit($showpoints, $showfeedback, $allow_scoring)) {
      $form['submit'] = array(
        '#type' => 'value',
        '#value' => $submit,
      );
    }
    if ($validate = $this->getReportFormValidate($showpoints, $showfeedback, $allow_scoring)) {
      $form['validate'] = array(
        '#type' => 'value',
        '#value' => $validate,
      );
    }
    $form['question'] = $this->getReportFormQuestion($showpoints, $showfeedback);
    $form['score'] = $this->getReportFormScore($showpoints, $showfeedback, $allow_scoring);
    $form['answer_feedback'] = $this->getReportFormAnswerFeedback($showpoints, $showfeedback, $allow_scoring);
    $form['max_score'] = array(
      '#type' => 'value',
      '#value' => $this->getMaxScore(),
    );
    $form['response'] = $this->getReportFormResponse($showpoints, $showfeedback, $allow_scoring);

    $form['#theme'] = $this->getReportFormTheme($showpoints, $showfeedback);
    $form['#is_correct'] = $this->isCorrect();
    $form['#is_evaluated'] = $this->isEvaluated();
    $form['#is_skipped'] = $this->is_skipped;
    return $form;
  }

  /**
   * get the question part of the reportForm
   *
   * @param $showpoints
   * @param $showfeedback
   * @return
   *  FAPI form array holding the question
   */
  public function getReportFormQuestion($showpoints = TRUE, $showfeedback = TRUE) {
    return array(
      '#type' => 'markup',
      '#value' => check_markup($this->question->body, $this->question->format, FALSE),
    );
  }

  /**
   * Get the response part of the report form
   *
   * @param $showpoints
   * @param $showfeedback
   * @param $allow_scoring
   * @return
   *  FAPI form array holding the response part
   */
  public function getReportFormResponse($showpoints = TRUE, $showfeedback = TRUE, $allow_scoring = FALSE) {
    return array(
      '#type' => 'markup',
      '#value' => ''
    );
  }

  /**
   * Get the score part of the report form
   *
   * @param $showpoints
   * @param $showfeedback
   * @param $allow_scoring
   * @return
   *  FAPI form array holding the score part
   */
  public function getReportFormScore($showpoints = TRUE, $showfeedback = TRUE, $allow_scoring = FALSE) {
    return array(
      '#type' => 'markup',
      '#value' => '<span class="quiz-report-score">'. $this->getScore() .'</span>',
    );
  }


  public function getReportFormAnswerFeedback($showpoints, $showfeedback, $allow_scoring) {
    return FALSE;
  }

  /**
   * Get the submit function for the reportForm
   *
   * @return
   *  Submit function as a string, or FALSE if no submit function
   */
  public function getReportFormSubmit($showfeedback = TRUE, $showpoints = TRUE, $allow_scoring = FALSE) {
    return FALSE;
  }

  /**
   * Get the validate function for the reportForm
   *
   * @return
   *  Validate function as a string, or FALSE if no validate function
   */
  public function getReportFormValidate($showfeedback = TRUE, $showpoints = TRUE, $allow_scoring = FALSE) {
    return FALSE;
  }

  /**
   * Get the theme key for the reportForm
   *
   * @return
   *  Theme key as a string, or FALSE if no submit function
   */
  public function getReportFormTheme($showfeedback = TRUE, $showpoints = TRUE) {
    return FALSE;
  }

  /**
   * Saves the quiz result. This is not used when a question is skipped!
   */
  public function saveResult() {
    $this->is_skipped = FALSE;
    $this->save();
  }

  /**
   * Save the current response.
   */
  abstract public function save();

  /**
   * Delete the response.
   */
  abstract public function delete();

  /**
   * Calculate the score for the response.
   */
  abstract public function score();

   /**
   * Get the user's response.
   */
  abstract public function getResponse();
}

