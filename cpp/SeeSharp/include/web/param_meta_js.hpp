// Метаданные параметров web UI: русские подсказки, списки допустимых
// значений (выпадающие списки вместо свободного ввода) и шаг/границы числовых
// полей. Подключается в kPage (main.cpp) после labels/hints/ruLabels и
// функции modes; чистые функции проверяет tests/param_meta.test.cjs
// (извлекает блок PARAMJS и гоняет в node:vm).
R"PARAMJS(
// Английские подсказки для ключей, у которых их не было.
Object.assign(hints,{
  color_model:'Color model in which the bounds are defined and evaluated: HSL, HSV, RGB, CMYK, HSLuv or YCbCr.',
  ui_language:'Language of the web interface: ru or en. Does not affect detection.',
  size:'Size criterion of a component. For base component 0 it is in pixels (per the selected size measure); for the others it is a ratio to the base component size (1 = same size).',
  model_rknn:'Path to the RKNN model used on MTV3 (RV1126 NPU); ignored on CM5 and host builds.',
  output_layout:'Layout of the YOLO output tensor: channels_first ([1, attributes, boxes]) or channels_last ([1, boxes, attributes]). Must match the exported model.',
  output_attributes:'Number of values per box in the YOLO output: 4 box coordinates + classes (+1 when objectness is present).',
  output_has_objectness:'Enable for YOLOv5-style outputs that carry a separate objectness score before the class scores.',
  format:'Metadata encoding for UDP; only json is supported.',
  rs485:'Toggle the RS-485 direction pin around each transmission.',
  startup_push:'Send DXL metadata automatically after start without a request from the controller.',
  push_interval_ms:'Minimum interval between automatic DXL metadata packets, in milliseconds.',
  eeprom_file:'File that keeps the DXL EEPROM register state between restarts.',
  system_admin:'Access to system administration endpoints of the web interface.',
  udp_metadata:'Detection metadata sent as UDP datagrams.',udp_video:'MJPEG video sent as UDP datagrams.',usb_stream:'Metadata and video over the USB gadget serial port.',uart_dxl:'Dynamixel-compatible (DXL) UART protocol.',
  score_threshold:'Minimum classifier confidence; weaker predictions are reported as no result.',input_size:'Side of the square classifier input in pixels; comes from the trained model.',
  classifier_epochs:'Default number of epochs suggested in the training tab.',classifier_image_size:'Side of the square sample image stored in the dataset and used for training.',
  max_dataset_mb:'Capture is refused once the dataset exceeds this size.',max_samples_per_class:'Capture is refused once a class has this many samples.',max_burst:'Largest number of frames one burst may capture.',min_burst_interval_ms:'Shortest allowed pause between burst frames.',max_upload_mb:'Largest ONNX file accepted by the model upload.',
  resize_size:'Shorter side after resizing and before the center crop.',center_crop:'Crop the center square instead of stretching the region to the input size.',swap_rb:'Swap the red and blue channels before inference.'
});

// Русские подсказки: что это за параметр, на что влияет и как его менять.
const ruHints={
  min_radius:'Наименьший радиус окружности в пикселях, который принимает детектор. Поставьте чуть меньше самого маленького ожидаемого круга.',
  max_radius:'Наибольший радиус окружности в пикселях. Поставьте чуть больше самого большого ожидаемого круга: узкий диапазон радиусов работает быстрее и даёт меньше ложных кругов.',
  distance:'Минимальное расстояние между центрами двух найденных окружностей в пикселях. Увеличьте, если один круг находится несколько раз.',
  hough_param1:'Верхний порог внутреннего детектора границ Canny. Увеличьте при шумном изображении: граница круга должна быть более резкой.',
  hough_param2:'Порог накопителя центров окружностей. Меньше — больше кругов, но и больше ложных срабатываний.',
  min_area:'Области с площадью контура меньше этого значения (пикс²) отбрасываются. Увеличьте, если находится мелкий шум.',
  max_area:'Области с площадью контура больше этого значения (пикс²) отбрасываются. Уменьшите, если за объект принимается большой участок фона.',
  min_width:'Минимальная ширина описанного прямоугольника в пикселях. Отсекает слишком тонкие области.',
  min_height:'Минимальная высота описанного прямоугольника в пикселях. Отсекает слишком низкие области.',
  min_circularity:'Минимальная округлость контура: 1 — идеальный круг, 0 — сильно неправильная форма. Поднимите до 0,6–0,8, чтобы искать только круглые объекты.',
  max_circularity:'Максимальная допустимая округлость контура. Опустите ниже 1, чтобы исключить круглые объекты.',
  min_inertia:'Минимальное отношение инерции: около 1 — круглые формы, около 0 — вытянутые. Поднимите, чтобы отсечь полосы и линии.',
  max_inertia:'Максимальное допустимое отношение инерции. Опустите, чтобы искать только вытянутые объекты.',
  min_convexity:'Минимальная выпуклость: площадь контура, делённая на площадь его выпуклой оболочки. Поднимите до 0,8–0,9, чтобы исключить рваные и вогнутые контуры.',
  max_convexity:'Максимальная допустимая выпуклость контура.',
  min_vertices:'Минимальное число вершин после аппроксимации контура многоугольником; 0 отключает ограничение. Например, 3 для треугольников.',
  max_vertices:'Максимальное число вершин после аппроксимации контура многоугольником; 0 отключает ограничение. Например, 4 для четырёхугольников.',
  polygon_approximation:'Точность аппроксимации контура многоугольником как доля периметра. Меньше — больше вершин сохраняется; обычно 0,01–0,05.',
  color_model:'Цветовая модель, в которой заданы и проверяются границы цвета: HSL, HSV, RGB, CMYK, HSLuv или YCbCr.',
  lower_range:'Нижняя граница цвета (включительно) по каналам выбранной цветовой модели. Удобнее задавать через редактор цвета ниже.',
  upper_range:'Верхняя граница цвета (включительно) по каналам выбранной цветовой модели.',
  min_luminance:'Минимальная яркость по каналу Y.',max_luminance:'Максимальная яркость по каналу Y.',
  min_chrominance_red:'Минимальное значение канала Cr.',max_chrominance_red:'Максимальное значение канала Cr.',min_chrominance_blue:'Минимальное значение канала Cb.',max_chrominance_blue:'Максимальное значение канала Cb.',
  enable_one_color_detection:'Поиск и вывод отдельных одноцветных областей. Для отключения конкретного цвета снимите флажок «Включено» у его шаблона.',
  enable_multicolor_detection:'Поиск связанных объектов, составленных из нескольких цветовых областей.',
  max_composite_objects:'Сколько лучших связанных объектов выводить за кадр (1–5).',
  one_color_patterns:'Описания отдельных цветовых шаблонов: диапазон цвета, размеры и форма области.',
  multicolor_patterns:'Описания связанных объектов: компоненты (цветовые шаблоны) и пространственные связи между ними.',
  blob_id:'ID цветовых шаблонов, которые могут выступать этим компонентом связанного объекта.',
  canny_threshold1:'Нижний порог гистерезиса детектора границ Canny. Слабые границы между нижним и верхним порогом принимаются, только если соединены с сильными. Увеличьте, если линий слишком много.',
  canny_threshold2:'Верхний порог гистерезиса Canny: границы сильнее этого значения принимаются всегда. Обычно в 2–3 раза больше нижнего порога.',
  canny_aperture_size:'Размер ядра Собеля для вычисления градиента в Canny: только 3, 5 или 7. 3 — тонкие границы и меньше шума, 7 — грубее, но устойчивее к размытию. Другие значения OpenCV не принимает.',
  canny_l2_gradient:'Точный евклидов модуль градиента (√(dx²+dy²)) вместо быстрой суммы |dx|+|dy|. Немного точнее и немного медленнее.',
  hough_rho:'Шаг по расстоянию накопителя Хафа в пикселях; обычно 1. Больше — быстрее, но грубее.',
  hough_theta:'Шаг по углу накопителя Хафа в радианах; 0,01745 соответствует 1°. Меньше — точнее угол, но медленнее.',
  hough_threshold:'Минимальное число голосов (точек границы) для принятия линии. Увеличьте, если линий слишком много.',
  hough_min_line_length:'Отрезки короче этой длины (пикс) отбрасываются. Увеличьте, чтобы убрать мелкие обрывки.',
  hough_max_line_gap:'Максимальный разрыв (пикс) между коллинеарными отрезками, которые объединяются в одну линию. Увеличьте, если линии рвутся.',
  min_angle:'Минимальная ориентация линии в градусах (−180…180). Вместе с максимальным углом оставляет линии нужного направления: например, 70…110 для вертикальных.',
  max_angle:'Максимальная ориентация линии в градусах (−180…180).',
  max_lines:'Максимальное число линий, выводимых за кадр, после всех фильтров.',
  roi_x:'Левая граница области поиска как доля ширины кадра, 0…1.',roi_y:'Верхняя граница области поиска как доля высоты кадра, 0…1. Для движения по линии обычно 0,5 (нижняя половина).',
  roi_width:'Ширина области поиска как доля ширины кадра, 0…1.',roi_height:'Высота области поиска как доля высоты кадра, 0…1.',
  dictionary:'Словарь ArUco, по которому напечатаны маркеры. Должен совпадать с напечатанными маркерами, иначе ID не определится.',
  marker_length:'Физическая длина стороны маркера в метрах; нужна для оценки положения.',
  allowed_ids:'Список разрешённых ID маркеров через запятую. Пустой список принимает любые ID.',
  model_onnx:'Путь к модели ONNX относительно рабочего каталога программы или абсолютный.',
  model_rknn:'Путь к модели RKNN для MTV3 (NPU RV1126); на CM5 и хосте не используется.',
  class_names_file:'Текстовый файл с названиями классов, по одному в строке, в порядке выходов модели.',
  class_names:'Список названий классов прямо в конфиге; используется, если внешний файл не задан.',
  input_width:'Ширина входа нейросети в пикселях; кадр масштабируется с сохранением пропорций (letterbox).',input_height:'Высота входа нейросети в пикселях.',
  confidence_threshold:'Минимальная уверенность класса (0…1), ниже которой объект отбрасывается до подавления дублей. Меняйте шагами по 0,05: больше — меньше ложных, меньше — меньше пропусков.',
  nms_threshold:'Порог IoU (пересечение/объединение, 0…1) для подавления пересекающихся рамок одного объекта. Уменьшите при дублирующихся рамках.',
  max_objects:'Максимальное число объектов, выводимых за кадр.',
  output_layout:'Расположение данных выходного тензора YOLO: channels_first ([1, атрибуты, рамки]) или channels_last ([1, рамки, атрибуты]). Должно соответствовать экспортированной модели.',
  output_attributes:'Число значений на одну рамку в выходе YOLO: 4 координаты + классы (+1, если есть objectness).',
  output_has_objectness:'Включите для выходов в стиле YOLOv5 с отдельной оценкой objectness перед оценками классов.',
  processing_mode:'Активный алгоритм обработки; удобнее переключать кнопками над параметрами.',
  ui_language:'Язык веб-интерфейса; на распознавание не влияет.',
  camera_rotation:'Поворот захваченного кадра по часовой стрелке: 0, 90, 180 или 270 градусов.',
  exposure_ev:'Компенсация экспозиции в ступенях EV; положительные значения осветляют изображение, отрицательные затемняют.',
  white_balance_bgr:'Множители каналов в порядке синий, зелёный, красный. 1, 1, 1 — без изменений.',
  contrast:'Множитель контраста; 1 оставляет контраст без изменений.',brightness:'Смещение яркости, прибавляемое к каждому каналу пикселя.',
  debug_mode:'Дополнительный диагностический вывод и отладочная разметка на результате.',
  enabled:'Включает этот шаблон, транспорт или подсистему. Отключённый цветовой шаблон игнорируется и одиночной, и составной детекцией.',
  host:'Имя хоста или IP-адрес получателя.',port:'Порт UDP/TCP получателя или прослушивания.',
  jpeg_quality:'Качество кодирования JPEG; больше — лучше качество и больше трафик.',packet_size:'Максимальный размер полезной нагрузки UDP-датаграммы в байтах.',
  max_fps:'Максимальное число видеокадров, передаваемых в секунду.',device:'Путь к устройству Linux для этого транспорта.',
  metadata:'Передавать по этому транспорту записи метаданных.',video:'Передавать по этому транспорту кодированные видеокадры.',
  baud:'Скорость UART в бит/с; на обеих сторонах должна быть одинаковой.',uart_binary:'Облегчённый бинарный протокол UART для передачи метаданных детекции.',
  format:'Формат метаданных UDP; поддерживается только json.',rs485:'Переключать линию направления RS-485 на время передачи.',
  startup_push:'Отправлять метаданные DXL автоматически после запуска без запроса контроллера.',push_interval_ms:'Минимальный интервал между автоматическими пакетами DXL в миллисекундах.',
  eeprom_file:'Файл, в котором хранится состояние регистров EEPROM DXL между перезапусками.',
  system_admin:'Доступ к системному администрированию через веб-интерфейс.',
  token:'Токен, который требуют привилегированные команды администрирования.',file_root:'Корень файловой системы, доступный файловому менеджеру.',terminal_enabled:'Разрешить выполнение команд терминала через API администрирования.',
  udp_metadata:'Метаданные детекции в виде UDP-датаграмм.',udp_video:'Видео MJPEG в виде UDP-датаграмм.',usb_stream:'Метаданные и видео через последовательный порт USB-гаджета.',uart_dxl:'Протокол UART, совместимый с Dynamixel (DXL).',
  data_yaml:'Описание датасета YOLO с путями train/val и названиями классов.',base_model:'Предобученная модель — отправная точка обучения.',
  epochs:'Число полных проходов по обучающему датасету.',image_size:'Сторона квадратного изображения при обучении.',output_onnx:'Куда сохранить обученную модель ONNX.',
  id:'Числовой идентификатор шаблона, объекта, компонента, транспорта или маркера.',
  threshold:'Минимальная нормированная оценка (0…1) для этого критерия. Уменьшите, если часть или связь не проходит.',
  overall_threshold:'Минимальная суммарная оценка (0…1) для принятия связанного объекта. Уменьшите, если части найдены, а объект нет.',
  weight:'Вклад критерия в общую оценку (0…255); 0 отключает критерий.',goal:'Идеальное значение критерия, за которое даётся максимальная оценка.',
  size:'Критерий размера компонента. Для базового компонента 0 — в пикселях (по выбранному способу измерения), для остальных — отношение к размеру базового компонента (1 — такой же размер).',size_measure:'Какая характеристика области сравнивается как размер: area (площадь), width, height, max_axis (наибольшая ось) или convex_area.',
  circularity:'Критерий округлости компонента.',inertia:'Критерий отношения инерции компонента.',convexity:'Критерий выпуклости компонента.',angle:'Критерий ориентации в градусах.',
  min:'Наименьшее значение, которое принимает критерий.',max:'Наибольшее значение, которое принимает критерий.',
  nodes:'Компоненты (цветовые области), из которых состоит связанный объект.',links:'Пространственные связи между компонентами: расстояние и направление.',
  length_absolute:'Допустимое расстояние между связанными компонентами в пикселях.',length_relative:'Допустимое расстояние относительно размера базового компонента.',
  angle_absolute:'Допустимое направление между связанными компонентами в градусах.',angle_relative:'Допустимое направление относительно базовой связи 0–1 в градусах.',
  web_preview:'Общий кеш MJPEG, который используют все веб-клиенты.',max_width:'Кадры шире этого значения уменьшаются перед кодированием JPEG.',
  region_mode:'whole — классифицируется весь кадр, roi — фиксированный прямоугольник, blob — каждая область, найденная одноцветным blob-детектором.',
  roi:'Нормированный прямоугольник [x, y, w, h] для режима roi и съёмки датасета.',blob_pattern_ids:'Только эти ID одноцветных шаблонов дают области; пустой список — все включённые шаблоны.',
  crop_padding:'Дополнительный отступ вокруг области blob перед классификацией и съёмкой; должен совпадать со значением при обучении.',
  max_regions:'Сначала классифицируются самые большие области; остальные пропускаются, чтобы ограничить время.',
  score_threshold:'Минимальная уверенность классификатора; слабые предсказания выводятся как «нет результата».',input_size:'Сторона квадратного входа классификатора в пикселях; берётся из обученной модели.',
  dataset_dir:'Каталог снятых примеров относительно config.json.',models_dir:'Каталог обученных и загруженных моделей ONNX относительно config.json.',
  python:'Интерпретатор с PyTorch для обучения классификатора на устройстве.',classifier_script:'Путь к simple_classifier.py на устройстве.',
  classifier_epochs:'Число эпох по умолчанию во вкладке «Обучение».',classifier_image_size:'Сторона квадратного примера в датасете и при обучении.',
  max_dataset_mb:'Съёмка отклоняется, когда датасет превышает этот размер.',max_samples_per_class:'Съёмка отклоняется, когда у класса столько примеров.',
  max_burst:'Наибольшее число кадров в одной серии.',min_burst_interval_ms:'Наименьшая пауза между кадрами серии.',max_upload_mb:'Наибольший размер загружаемого файла ONNX.',
  require_admin_token:'Требовать токен system_admin для /training так же, как для /admin.',
  resize_size:'Короткая сторона после масштабирования и до центральной обрезки.',center_crop:'Вырезать центральный квадрат вместо растяжения области до размера входа.',swap_rb:'Поменять местами красный и синий каналы перед инференсом.'
};

// Поля с фиксированным набором значений: вместо свободного ввода — список.
// Элемент — значение или [значение, подпись].
const paramOptions={
  canny_aperture_size:[3,5,7],
  camera_rotation:[0,90,180,270],
  ui_language:[['ru','Русский'],['en','English']],
  size_measure:['area','width','height','max_axis','convex_area'],
  output_layout:['channels_first','channels_last'],
  region_mode:['whole','roi','blob'],
  format:['json'],
  dictionary:['DICT_4X4_50','DICT_4X4_100','DICT_4X4_250','DICT_4X4_1000','DICT_5X5_50','DICT_5X5_100','DICT_5X5_250','DICT_5X5_1000','DICT_6X6_50','DICT_6X6_100','DICT_6X6_250','DICT_6X6_1000','DICT_7X7_50','DICT_7X7_100','DICT_7X7_250','DICT_7X7_1000','DICT_ARUCO_ORIGINAL']
};
// Шаг и границы числовых полей: [step, min, max]. Дробные параметры без
// явного шага получали бы шаг 1 от браузера и «прыгали» по целым.
const paramSteps={
  confidence_threshold:[0.01,0,1],nms_threshold:[0.01,0,1],score_threshold:[0.01,0,1],threshold:[0.01,0,1],overall_threshold:[0.01,0,1],
  min_circularity:[0.01,0,1],max_circularity:[0.01,0,1],min_inertia:[0.01,0,1],max_inertia:[0.01,0,1],min_convexity:[0.01,0,1],max_convexity:[0.01,0,1],
  polygon_approximation:[0.001,0,1],crop_padding:[0.01,0,1],
  roi_x:[0.01,0,1],roi_y:[0.01,0,1],roi_width:[0.01,0,1],roi_height:[0.01,0,1],
  canny_threshold1:[1,0],canny_threshold2:[1,0],hough_rho:[0.1,0.1],hough_theta:[0.0001,0.0001],hough_threshold:[1,1],hough_min_line_length:[1,0],hough_max_line_gap:[1,0],
  min_angle:[1,-180,180],max_angle:[1,-180,180],max_lines:[1,1],
  min_radius:[1,0],max_radius:[1,0],hough_param1:[1,1],hough_param2:[1,1],distance:[1,1],
  min_area:[1,0],max_area:[1,0],min_width:[1,0],min_height:[1,0],min_vertices:[1,0],max_vertices:[1,0],
  marker_length:[0.001,0],exposure_ev:[0.1,-5,5],contrast:[0.05,0],brightness:[1,-255,255],
  weight:[1,0,255],max_composite_objects:[1,1,5],max_objects:[1,1],max_regions:[1,1],
  jpeg_quality:[1,1,100],max_fps:[1,1],max_width:[1,1],packet_size:[1,1],port:[1,1,65535],baud:[1,1],push_interval_ms:[1,1],
  input_width:[1,1],input_height:[1,1],input_size:[1,1],epochs:[1,1],image_size:[1,1],
  // критерии связанных объектов: min/max/goal зависят от родителя (см. fieldMeta)
};
const parentSteps={circularity:[0.01,0,1],inertia:[0.01,0,1],convexity:[0.01,0,1],angle:[1,-180,180],angle_absolute:[1,-180,180],angle_relative:[1,-180,180],length_absolute:[1,0],length_relative:[0.01,0]};
// Размер базового компонента (nodes[0]) задаётся в пикселях, остальных — как
// отношение к базовому (см. blob_processor.cpp: baseNodeSize).
function sizeSpec(path){const nodes=path.indexOf('nodes');return nodes>=0&&Number(path[nodes+1])===0?[1,0]:[0.01,0];}
// Шаг по числу знаков после запятой: 0.35 -> 0.01, 84.85 -> 0.01, 7 -> 1.
function decimalStep(value){
  if(!Number.isFinite(value)||Number.isInteger(value))return 1;
  const text=String(value),point=text.indexOf('.');
  if(point<0||/e/i.test(text))return 0.0001;
  return Number('1e-'+Math.min(4,text.length-point-1));   // не Math.pow: 10^-4 даёт 9.99e-5
}
// Описание поля формы: {options} для списка, {step,min,max} для числа.
function fieldMeta(key,path,value){
  if(key==='processing_mode')return{options:modes.map(m=>[m,(modeNames[language]||modeNames.en)[m]||m])};
  if(paramOptions[key])return{options:paramOptions[key].map(o=>Array.isArray(o)?o:[o,String(o)])};
  if(typeof value!=='number')return{};
  const parent=path.length>=2?path[path.length-2]:undefined;
  const criterion=['min','max','goal'].includes(key);
  const spec=paramSteps[key]||(criterion&&parent==='size'?sizeSpec(path):null)||(criterion&&parentSteps[parent])||null;
  const meta={step:spec?spec[0]:decimalStep(value)};
  if(spec&&spec.length>1)meta.min=spec[1];
  if(spec&&spec.length>2)meta.max=spec[2];
  return meta;
}
)PARAMJS"
